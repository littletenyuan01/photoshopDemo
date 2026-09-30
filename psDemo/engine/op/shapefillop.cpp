/**
 * shapefillop.cpp — shapefillop.h 实现（engine/op 层）。
 *
 * 抽出包围盒补丁 → QPainter 绘制 → 写回瓦片；有选区时回滚选区外像素。
 */
#include "shapefillop.h"

#include "paintclip.h"
#include "domain/tilebuffer.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>
#include <cstring>

namespace Ps {

namespace {

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

QRectF localRect(const QRectF &docRect, const QRect &padOrigin)
{
    return docRect.translated(-padOrigin.x(), -padOrigin.y());
}

} // namespace

bool ShapeFillOp::prepare(OpContext &ctx)
{
    if (!BufferOp::prepare(ctx))
        return false;
    if (!m_fill && !m_stroke)
        return false;
    if (m_rect.width() < 0.5 && m_rect.height() < 0.5 && m_kind != ShapeKind::Line)
        return false;
    return ctx.tiles->width() > 0 && ctx.tiles->height() > 0;
}

QRect ShapeFillOp::process(OpContext &ctx)
{
    TileBuffer &tiles = *ctx.tiles;
    const PaintSelectionClip &clip = ctx.clip;
    const QRect layerRect(0, 0, tiles.width(), tiles.height());

    const qreal pad = m_stroke ? (m_strokeWidth * 0.5 + 2.0) : 2.0;
    QRectF geom = m_rect.normalized();
    if (m_kind == ShapeKind::Line) {
        // 直线：m_rect 的对角为两端点（不必 normalized 交换语义）
        geom = QRectF(m_rect.topLeft(), m_rect.bottomRight());
    }
    QRect bounds = geom.adjusted(-pad, -pad, pad, pad).toAlignedRect().intersected(layerRect);
    if (bounds.isEmpty())
        return {};

    QRect workRect = bounds;
    if (OpPaintClip::clipActive(clip)) {
        workRect = workRect.intersected(
            OpPaintClip::selectionRectInLayer(clip, layerRect.width(), layerRect.height()));
        if (workRect.isEmpty())
            return {};
    }

    QImage before;
    if (OpPaintClip::clipActive(clip))
        before = extractPatch(tiles, workRect);

    QImage patch = extractPatch(tiles, bounds);
    QPainter painter(&patch);
    painter.setRenderHint(QPainter::Antialiasing, m_antialias);

    const QColor brushColor = m_color;
    QPen pen(brushColor);
    pen.setWidthF(qMax(1.0, m_strokeWidth));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    const QRectF local = localRect(geom.normalized(), bounds);
    const QPointF p0 = QPointF(m_rect.left(), m_rect.top()) - QPointF(bounds.x(), bounds.y());
    const QPointF p1 = QPointF(m_rect.right(), m_rect.bottom()) - QPointF(bounds.x(), bounds.y());

    if (m_kind == ShapeKind::Line) {
        painter.setPen(pen);
        painter.drawLine(p0, p1);
    } else {
        if (m_fill) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(brushColor);
            if (m_kind == ShapeKind::Rect) {
                if (m_cornerRadius > 0.5)
                    painter.drawRoundedRect(local, m_cornerRadius, m_cornerRadius);
                else
                    painter.drawRect(local);
            } else if (m_kind == ShapeKind::Ellipse) {
                painter.drawEllipse(local);
            } else { // Triangle：顶边中点 + 底边两端
                QPainterPath path;
                path.moveTo(local.center().x(), local.top());
                path.lineTo(local.bottomRight());
                path.lineTo(local.bottomLeft());
                path.closeSubpath();
                painter.drawPath(path);
            }
        }
        if (m_stroke) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(pen);
            if (m_kind == ShapeKind::Rect) {
                if (m_cornerRadius > 0.5)
                    painter.drawRoundedRect(local, m_cornerRadius, m_cornerRadius);
                else
                    painter.drawRect(local);
            } else if (m_kind == ShapeKind::Ellipse) {
                painter.drawEllipse(local);
            } else {
                QPainterPath path;
                path.moveTo(local.center().x(), local.top());
                path.lineTo(local.bottomRight());
                path.lineTo(local.bottomLeft());
                path.closeSubpath();
                painter.drawPath(path);
            }
        }
    }
    painter.end();

    if (OpPaintClip::clipActive(clip)) {
        const QPoint rel = workRect.topLeft() - bounds.topLeft();
        blitPatch(tiles, workRect, patch.copy(QRect(rel, workRect.size())));
        tiles.forEachTileInRect(workRect, true, [&](int, int, QImage &tile, const QRect &tb) {
            const QRect area = workRect.intersected(tb);
            if (area.isEmpty())
                return;
            const QPoint inTile = area.topLeft() - tb.topLeft();
            const QPoint inBefore = area.topLeft() - workRect.topLeft();
            OpPaintClip::restoreOutsideSelection(
                tile, before.copy(QRect(inBefore, area.size())), inTile, area.topLeft(), clip);
        });
    } else {
        blitPatch(tiles, bounds, patch);
    }

    return workRect;
}

} // namespace Ps
