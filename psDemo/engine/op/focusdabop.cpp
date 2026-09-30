/**
 * focusdabop.cpp — focusdabop.h 实现（engine/op 层）。
 *
 * 先抽出 dab 邻域补丁，卷积/涂抹后再写回瓦片；选区用子区备份回滚。
 */
#include "focusdabop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"

#include <QtMath>
#include <cstring>

namespace Ps {

namespace {

constexpr int kBlurRadius = 2; // 5×5 盒模糊

qreal brushCover(qreal dist, qreal radius, qreal hardness)
{
    if (radius <= 0.0 || dist >= radius)
        return 0.0;
    hardness = qBound(0.0, hardness, 1.0);
    const qreal stop = qBound(0.05, hardness, 0.98) * radius;
    if (dist <= stop)
        return 1.0;
    const qreal t = (dist - stop) / (radius - stop);
    return qBound(0.0, 1.0 - t, 1.0);
}

/** 从瓦片稀疏缓冲拷贝矩形到独立图像（层内坐标）。 */
QImage extractPatch(const TileBuffer &tiles, const QRect &rect)
{
    QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(0);
    if (rect.isEmpty())
        return out;

    const int x0 = rect.left() / TileBuffer::kTileSize;
    const int y0 = rect.top() / TileBuffer::kTileSize;
    const int x1 = rect.right() / TileBuffer::kTileSize;
    const int y1 = rect.bottom() / TileBuffer::kTileSize;

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            const QImage *tile = tiles.tileAt(tx, ty);
            if (!tile)
                continue;
            const QRect bounds = tiles.tileBounds(tx, ty);
            const QRect overlap = bounds.intersected(rect);
            if (overlap.isEmpty())
                continue;
            const QPoint srcTL = overlap.topLeft() - bounds.topLeft();
            const QPoint dstTL = overlap.topLeft() - rect.topLeft();
            for (int y = 0; y < overlap.height(); ++y) {
                const QRgb *src = reinterpret_cast<const QRgb *>(tile->constScanLine(srcTL.y() + y))
                                  + srcTL.x();
                QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(dstTL.y() + y)) + dstTL.x();
                memcpy(dst, src, size_t(overlap.width()) * sizeof(QRgb));
            }
        }
    }
    return out;
}

/** 把补丁写回瓦片（allocateMissing）。 */
void blitPatch(TileBuffer &tiles, const QRect &rect, const QImage &patch)
{
    if (rect.isEmpty() || patch.size() != rect.size())
        return;
    tiles.forEachTileInRect(rect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QRect overlap = bounds.intersected(rect);
        if (overlap.isEmpty())
            return;
        const QPoint srcTL = overlap.topLeft() - rect.topLeft();
        const QPoint dstTL = overlap.topLeft() - bounds.topLeft();
        for (int y = 0; y < overlap.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(patch.constScanLine(srcTL.y() + y))
                              + srcTL.x();
            QRgb *dst = reinterpret_cast<QRgb *>(tile.scanLine(dstTL.y() + y)) + dstTL.x();
            memcpy(dst, src, size_t(overlap.width()) * sizeof(QRgb));
        }
    });
}

QRgb sampleClamped(const QImage &img, int x, int y)
{
    x = qBound(0, x, img.width() - 1);
    y = qBound(0, y, img.height() - 1);
    return reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
}

/** 5×5 盒模糊（预乘通道；边界钳制）。 */
QRgb boxBlur5(const QImage &img, int x, int y)
{
    qint64 r = 0, g = 0, b = 0, a = 0;
    int n = 0;
    for (int dy = -kBlurRadius; dy <= kBlurRadius; ++dy) {
        for (int dx = -kBlurRadius; dx <= kBlurRadius; ++dx) {
            const QRgb px = sampleClamped(img, x + dx, y + dy);
            r += qRed(px);
            g += qGreen(px);
            b += qBlue(px);
            a += qAlpha(px);
            ++n;
        }
    }
    return qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
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
    // orig + amount * (orig - blurred)
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

    // 读抹需按 delta 采样；模糊/锐化需内核邻域
    int pad = kBlurRadius;
    if (m_mode == FocusMode::Smudge) {
        pad = qMax(pad, qCeil(qAbs(m_smudgeDelta.x())) + 1);
        pad = qMax(pad, qCeil(qAbs(m_smudgeDelta.y())) + 1);
    }
    const QRect padded = dabRect.adjusted(-pad, -pad, pad, pad).intersected(layerRect);
    const QImage src = extractPatch(tiles, padded);
    if (src.isNull())
        return {};

    QImage dst = src.copy();
    const qreal strength = qBound(0.0, m_strength, 1.0);
    const QPointF centerInPatch(m_center.x() - padded.x(), m_center.y() - padded.y());

    const int x0 = dabRect.left() - padded.left();
    const int y0 = dabRect.top() - padded.top();
    const int x1 = dabRect.right() - padded.left();
    const int y1 = dabRect.bottom() - padded.top();

    for (int py = y0; py <= y1; ++py) {
        QRgb *outLine = reinterpret_cast<QRgb *>(dst.scanLine(py));
        for (int px = x0; px <= x1; ++px) {
            const qreal dx = px + 0.5 - centerInPatch.x();
            const qreal dy = py + 0.5 - centerInPatch.y();
            const qreal cover = brushCover(qSqrt(dx * dx + dy * dy), m_radius, m_hardness);
            if (cover <= 0.0)
                continue;

            const QRgb orig = reinterpret_cast<const QRgb *>(src.constScanLine(py))[px];
            QRgb result = orig;
            const qreal amt = strength * cover;

            if (m_mode == FocusMode::Blur) {
                result = lerpPremul(orig, boxBlur5(src, px, py), amt);
            } else if (m_mode == FocusMode::Sharpen) {
                result = lerpPremul(orig, sharpenMix(orig, boxBlur5(src, px, py), 1.0), amt);
            } else {
                // 涂抹：从「上一笔尖」方向取样并混入
                const int sx = qRound(px + m_smudgeDelta.x());
                const int sy = qRound(py + m_smudgeDelta.y());
                const QRgb pulled = sampleClamped(src, sx, sy);
                result = lerpPremul(orig, pulled, amt);
            }
            outLine[px] = result;
        }
    }

    // 有选区：只提交 workRect 内、且选中的像素
    if (OpPaintClip::clipActive(clip)) {
        QImage before = extractPatch(tiles, workRect);
        const QRect rel = QRect(workRect.topLeft() - padded.topLeft(), workRect.size());
        blitPatch(tiles, workRect, dst.copy(rel));
        // 回滚选区外：从 before 恢复
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
        blitPatch(tiles, dabRect,
                  dst.copy(QRect(dabRect.topLeft() - padded.topLeft(), dabRect.size())));
    }

    return workRect;
}

} // namespace Ps
