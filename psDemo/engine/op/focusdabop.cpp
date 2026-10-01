/**
 * focusdabop.cpp — focusdabop.h 实现（engine/op 层）。
 *
 * 模糊/锐化：整补丁一次可分离盒模糊（O(WH)），再按盖度混合——避免每像素 5×5。
 * 涂抹：按 delta 取样。选区用子区备份回滚。
 */
#include "focusdabop.h"

#include "brushcover.h"
#include "paintclip.h"
#include "tilepatch.h"
#include "domain/tilebuffer.h"

#include <QtMath>

namespace Ps {

namespace {

constexpr int kBlurRadius = 2; // 5×5 盒模糊

QRgb sampleClamped(const QImage &img, int x, int y)
{
    x = qBound(0, x, img.width() - 1);
    y = qBound(0, y, img.height() - 1);
    return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
}

/**
 * 可分离 5×5 盒模糊：横扫再纵扫，整图 O(WH)，替代每像素 25 次取样。
 * 输出与 src 同尺寸；边界钳制。
 */
QImage boxBlurImage5(const QImage &src)
{
    const int w = src.width();
    const int h = src.height();
    QImage temp(w, h, QImage::Format_ARGB32_Premultiplied);
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    if (w <= 0 || h <= 0)
        return out;

    // 横向：对每个像素累加 x±2（钳制），均值写 temp
    for (int y = 0; y < h; ++y) {
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *tline = reinterpret_cast<QRgb *>(temp.scanLine(y));
        for (int x = 0; x < w; ++x) {
            qint64 r = 0, g = 0, b = 0, a = 0;
            for (int dx = -kBlurRadius; dx <= kBlurRadius; ++dx) {
                const int sx = qBound(0, x + dx, w - 1);
                const QRgb px = sline[sx];
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
                a += qAlpha(px);
            }
            constexpr int n = kBlurRadius * 2 + 1;
            tline[x] = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
        }
    }

    // 纵向：对 temp 的 y±2 均值写 out
    for (int y = 0; y < h; ++y) {
        QRgb *oline = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            qint64 r = 0, g = 0, b = 0, a = 0;
            for (int dy = -kBlurRadius; dy <= kBlurRadius; ++dy) {
                const int sy = qBound(0, y + dy, h - 1);
                const QRgb px = reinterpret_cast<const QRgb *>(temp.constScanLine(sy))[x];
                r += qRed(px);
                g += qGreen(px);
                b += qBlue(px);
                a += qAlpha(px);
            }
            constexpr int n = kBlurRadius * 2 + 1;
            oline[x] = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
        }
    }
    return out;
}

QRgb lerpPremul(QRgb a, QRgb b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    const qreal u = 1.0 - t;
    return qRgba(qBound(0, qRound(qRed(a) * u + qRed(b) * t), 255),
                 qBound(0, qRound(qGreen(a) * u + qGreen(b) * t), 255),
                 qBound(0, qRound(qBlue(a) * u + qBlue(b) * t), 255),
                 qBound(0, qRound(qAlpha(a) * u + qAlpha(b) * t), 255));
}

QRgb sharpenMix(QRgb orig, QRgb blurred, qreal amount)
{
    const int r = qBound(0, qRound(qRed(orig) + amount * (qRed(orig) - qRed(blurred))), 255);
    const int g = qBound(0, qRound(qGreen(orig) + amount * (qGreen(orig) - qGreen(blurred))), 255);
    const int b = qBound(0, qRound(qBlue(orig) + amount * (qBlue(orig) - qBlue(blurred))), 255);
    const int a = qBound(0, qRound(qAlpha(orig) + amount * (qAlpha(orig) - qAlpha(blurred))), 255);
    return qRgba(r, g, b, a);
}

} // namespace

QRect FocusDabOp::dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

bool FocusDabOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx) || m_radius <= 0.0)
        return false;
    return ctx.tiles->width() > 0 && ctx.tiles->height() > 0;
}

QRect FocusDabOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());
    const QRect dabRect = dabBounds(m_center, m_radius).intersected(layerRect);
    if (dabRect.isEmpty())
        return {};

    QRect workRect = dabRect;
    if (OpPaintClip::clipActive(clip)) {
        workRect = workRect.intersected(
            OpPaintClip::selectionRectInLayer(clip, layerRect.width(), layerRect.height()));
        if (workRect.isEmpty())
            return {};
    }

    int pad = (m_mode == FocusMode::Smudge) ? 0 : kBlurRadius;
    if (m_mode == FocusMode::Smudge) {
        pad = qMax(1, qCeil(qAbs(m_smudgeDelta.x())) + 1);
        pad = qMax(pad, qCeil(qAbs(m_smudgeDelta.y())) + 1);
    }
    const QRect padded = dabRect.adjusted(-pad, -pad, pad, pad).intersected(layerRect);
    const QImage src = TilePatch::extract(tiles, padded);
    if (src.isNull())
        return {};

    // 模糊/锐化：整补丁预模糊一次（热点优化）
    QImage blurred;
    if (m_mode == FocusMode::Blur || m_mode == FocusMode::Sharpen)
        blurred = boxBlurImage5(src);

    QImage dst = src; // COW；写时分离
    const qreal strength = qBound(0.0, m_strength, 1.0);
    const QPointF centerInPatch(m_center.x() - padded.x(), m_center.y() - padded.y());

    const int x0 = dabRect.left() - padded.left();
    const int y0 = dabRect.top() - padded.top();
    const int x1 = dabRect.right() - padded.left();
    const int y1 = dabRect.bottom() - padded.top();

    for (int py = y0; py <= y1; ++py) {
        QRgb *outLine = reinterpret_cast<QRgb *>(dst.scanLine(py));
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(src.constScanLine(py));
        const QRgb *blurLine = blurred.isNull()
                                   ? nullptr
                                   : reinterpret_cast<const QRgb *>(blurred.constScanLine(py));
        for (int px = x0; px <= x1; ++px) {
            const qreal dx = px + 0.5 - centerInPatch.x();
            const qreal dy = py + 0.5 - centerInPatch.y();
            const qreal cover = BrushCover::fromDist2(dx * dx + dy * dy, m_radius, m_hardness);
            if (cover <= 0.0)
                continue;

            const QRgb orig = srcLine[px];
            QRgb result = orig;
            const qreal amt = strength * cover;

            if (m_mode == FocusMode::Blur) {
                result = lerpPremul(orig, blurLine[px], amt);
            } else if (m_mode == FocusMode::Sharpen) {
                result = lerpPremul(orig, sharpenMix(orig, blurLine[px], 1.0), amt);
            } else {
                const int sx = qRound(px + m_smudgeDelta.x());
                const int sy = qRound(py + m_smudgeDelta.y());
                result = lerpPremul(orig, sampleClamped(src, sx, sy), amt);
            }
            outLine[px] = result;
        }
    }

    if (OpPaintClip::clipActive(clip)) {
        QImage before = TilePatch::extract(tiles, workRect);
        const QRect rel = QRect(workRect.topLeft() - padded.topLeft(), workRect.size());
        TilePatch::blit(tiles, workRect, dst.copy(rel));
        tiles.forEachTileInRect(workRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
            const QRect area = workRect.intersected(bounds);
            if (area.isEmpty())
                return;
            const QPoint inTile = area.topLeft() - bounds.topLeft();
            const QPoint inBefore = area.topLeft() - workRect.topLeft();
            const QImage beforeSub = before.copy(QRect(inBefore, area.size()));
            OpPaintClip::restoreOutsideSelection(tile, beforeSub, inTile, area.topLeft(), clip);
        });
    } else {
        TilePatch::blit(tiles, dabRect,
                        dst.copy(QRect(dabRect.topLeft() - padded.topLeft(), dabRect.size())));
    }

    return workRect;
}

} // namespace Ps
