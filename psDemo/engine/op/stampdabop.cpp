/**
 * stampdabop.cpp — stampdabop.h 实现（engine/op 层）。
 *
 * QRadialGradient 硬/软边 dab；遍历缩至 dab ∩ 选区外接框。
 */
#include "stampdabop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"

#include <QPainter>
#include <QtMath>

namespace Ps {

namespace {

void stampDabOnImageRaw(QImage &target,
                        const QPointF &center,
                        qreal radius,
                        const QColor &color,
                        PaintMode mode,
                        qreal hardness)
{
    if (target.isNull() || radius <= 0.0)
        return;

    hardness = qBound(0.0, hardness, 1.0);

    const QRect clip = StampDabOp::dabBounds(center, radius).intersected(target.rect());
    if (clip.isEmpty())
        return;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clip);

    if (mode == PaintMode::Erase)
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    else
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QColor core = (mode == PaintMode::Erase) ? QColor(255, 255, 255) : color;
    QColor edge = core;
    core.setAlpha(255);
    edge.setAlpha(0);

    QRadialGradient grad(center, radius);
    const qreal stop = qBound(0.05, hardness, 0.98);
    grad.setColorAt(0.0, core);
    grad.setColorAt(stop, core);
    grad.setColorAt(1.0, edge);

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawEllipse(center, radius, radius);
}

} // namespace

QRect StampDabOp::dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

bool StampDabOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx) || m_radius <= 0.0)
        return false;
    TileBuffer &tiles = *ctx.tiles;
    return tiles.width() > 0 && tiles.height() > 0;
}

QRect StampDabOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());
    const QRect dabRect = dabBounds(m_center, m_radius).intersected(layerRect);
    if (dabRect.isEmpty())
        return {};

    // 有选区时把遍历缩到「dab ∩ 选区外接框」：选区外的像素反正会被回滚，不必去画。
    // ctx.roi 不参与——dab 包围盒本身就是一次绘制的最小窗口。
    QRect workRect = dabRect;
    if (OpPaintClip::clipActive(clip)) {
        workRect = workRect.intersected(
            OpPaintClip::selectionRectInLayer(clip, layerRect.width(), layerRect.height()));
        if (workRect.isEmpty())
            return {};
    }

    tiles.forEachTileInRect(workRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QPointF local(m_center.x() - bounds.x(), m_center.y() - bounds.y());
        if (!OpPaintClip::clipActive(clip)) {
            stampDabOnImageRaw(tile, local, m_radius, m_color, m_mode, m_hardness);
            return;
        }
        // 只备份 / 回滚 dab 真正覆盖到的子区（旧实现深拷整块瓦片 + 逐像素跑满整块）
        const QRect area = dabRect.intersected(bounds);
        if (area.isEmpty())
            return;
        const QPoint inTile = area.topLeft() - bounds.topLeft();
        const QImage before = tile.copy(QRect(inTile, area.size()));
        stampDabOnImageRaw(tile, local, m_radius, m_color, m_mode, m_hardness);
        OpPaintClip::restoreOutsideSelection(tile, before, inTile, area.topLeft(), clip);
    });

    return workRect;
}

} // namespace Ps
