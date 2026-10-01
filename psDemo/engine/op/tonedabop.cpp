/**
 * tonedabop.cpp — tonedabop.h 实现（engine/op 层）。
 *
 * 减淡/海绵：抽补丁 → 按盖度调色 → 写回；BrushCover dist² 早退。
 */
#include "tonedabop.h"

#include "brushcover.h"
#include "paintclip.h"
#include "tilepatch.h"
#include "engine/premul.h"
#include "domain/tilebuffer.h"

#include <QtMath>

namespace Ps {

namespace {

QRgb applyDodge(QRgb px, qreal amount)
{
    int r, g, b, a;
    Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    if (a <= 0)
        return px;
    r = qBound(0, qRound(r + (255 - r) * amount), 255);
    g = qBound(0, qRound(g + (255 - g) * amount), 255);
    b = qBound(0, qRound(b + (255 - b) * amount), 255);
    return Premul::toPremultipliedRgb(QColor(r, g, b, a));
}

QRgb applySponge(QRgb px, qreal amount)
{
    int r, g, b, a;
    Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    if (a <= 0)
        return px;
    const qreal gray = 0.299 * r + 0.587 * g + 0.114 * b;
    r = qBound(0, qRound(gray + (r - gray) * (1.0 + amount)), 255);
    g = qBound(0, qRound(gray + (g - gray) * (1.0 + amount)), 255);
    b = qBound(0, qRound(gray + (b - gray) * (1.0 + amount)), 255);
    return Premul::toPremultipliedRgb(QColor(r, g, b, a));
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

    const QImage src = TilePatch::extract(tiles, dabRect);
    QImage dst = src; // COW
    const qreal strength = qBound(0.0, m_strength, 1.0);
    const QPointF centerInPatch(m_center.x() - dabRect.x(), m_center.y() - dabRect.y());
    const bool dodge = (m_mode == ToneMode::Dodge);

    for (int py = 0; py < dst.height(); ++py) {
        QRgb *line = reinterpret_cast<QRgb *>(dst.scanLine(py));
        const QRgb *srcLine = reinterpret_cast<const QRgb *>(src.constScanLine(py));
        for (int px = 0; px < dst.width(); ++px) {
            const qreal dx = px + 0.5 - centerInPatch.x();
            const qreal dy = py + 0.5 - centerInPatch.y();
            const qreal cover = BrushCover::fromDist2(dx * dx + dy * dy, m_radius, m_hardness);
            if (cover <= 0.0)
                continue;
            const qreal amt = strength * cover;
            line[px] = dodge ? applyDodge(srcLine[px], amt) : applySponge(srcLine[px], amt);
        }
    }

    if (OpPaintClip::clipActive(clip)) {
        QImage before = TilePatch::extract(tiles, workRect);
        const QPoint rel = workRect.topLeft() - dabRect.topLeft();
        TilePatch::blit(tiles, workRect, dst.copy(QRect(rel, workRect.size())));
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
        TilePatch::blit(tiles, dabRect, dst);
    }

    return workRect;
}

} // namespace Ps
