/**
 * tonedabop.cpp — tonedabop.h 实现（engine/op 层）。
 */
#include "tonedabop.h"

#include "paintclip.h"
#include "engine/premul.h"
#include "domain/tilebuffer.h"

#include <QtMath>
#include <cstring>

namespace Ps {

namespace {

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

QRgb applyDodge(QRgb px, qreal amount)
{
    int r, g, b, a;
    Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    if (a <= 0)
        return px;
    // 向白提亮（对照 dodge highlights 精简）
    r = qBound(0, qRound(r + (255 - r) * amount), 255);
    g = qBound(0, qRound(g + (255 - g) * amount), 255);
    b = qBound(0, qRound(b + (255 - b) * amount), 255);
    return Premul::toPremultipliedArgb(QColor(r, g, b, a));
}

QRgb applySponge(QRgb px, qreal amount)
{
    int r, g, b, a;
    Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    if (a <= 0)
        return px;
    // 相对灰阶拉开通道 → 提高饱和度
    const qreal gray = 0.299 * r + 0.587 * g + 0.114 * b;
    r = qBound(0, qRound(gray + (r - gray) * (1.0 + amount)), 255);
    g = qBound(0, qRound(gray + (g - gray) * (1.0 + amount)), 255);
    b = qBound(0, qRound(gray + (b - gray) * (1.0 + amount)), 255);
    return Premul::toPremultipliedArgb(QColor(r, g, b, a));
}

} // namespace

QRect ToneDabOp::dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

bool ToneDabOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx) || m_radius <= 0.0)
        return false;
    return ctx.tiles->width() > 0 && ctx.tiles->height() > 0;
}

QRect ToneDabOp::process(OpContext &ctx)
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

    const QImage src = extractPatch(tiles, dabRect);
    QImage dst = src.copy();
    const qreal strength = qBound(0.0, m_strength, 1.0);
    const QPointF centerInPatch(m_center.x() - dabRect.x(), m_center.y() - dabRect.y());

    for (int py = 0; py < dst.height(); ++py) {
        QRgb *line = reinterpret_cast<QRgb *>(dst.scanLine(py));
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(src.constScanLine(py));
        for (int px = 0; px < dst.width(); ++px) {
            const qreal dx = px + 0.5 - centerInPatch.x();
            const qreal dy = py + 0.5 - centerInPatch.y();
            const qreal cover = brushCover(qSqrt(dx * dx + dy * dy), m_radius, m_hardness);
            if (cover <= 0.0)
                continue;
            const qreal amt = strength * cover;
            line[px] = (m_mode == ToneMode::Dodge)
                           ? applyDodge(srcLine[px], amt)
                           : applySponge(srcLine[px], amt);
        }
    }

    if (OpPaintClip::clipActive(clip)) {
        QImage before = extractPatch(tiles, workRect);
        const QPoint rel = workRect.topLeft() - dabRect.topLeft();
        blitPatch(tiles, workRect, dst.copy(QRect(rel, workRect.size())));
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
        blitPatch(tiles, dabRect, dst);
    }

    return workRect;
}

} // namespace Ps
