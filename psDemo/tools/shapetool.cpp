/**
 * shapetool.cpp — ShapeTool 实现（tools 层）。
 */
#include "shapetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/paintengine.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ps {

namespace {

PaintEngine::SelectionClip selectionClipFor(Layer *layer, ImageDocument *doc)
{
    PaintEngine::SelectionClip clip;
    if (!layer || !doc)
        return clip;
    clip.selection = &doc->selection();
    clip.layerOffsetX = layer->offsetX();
    clip.layerOffsetY = layer->offsetY();
    return clip;
}

} // namespace

ShapeTool::ShapeTool(Ps::ToolId id, ShapeKind kind, QObject *parent)
    : Tool(id, parent)
    , m_kind(kind)
{
}

Qt::CursorShape ShapeTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool ShapeTool::hasOverlay() const
{
    return m_dragging;
}

QString ShapeTool::undoLabel() const
{
    switch (m_kind) {
    case ShapeKind::Rect: return QObject::tr("矩形");
    case ShapeKind::Ellipse: return QObject::tr("椭圆");
    case ShapeKind::Triangle: return QObject::tr("三角形");
    case ShapeKind::Line: return QObject::tr("直线");
    }
    return QObject::tr("形状");
}

QPointF ShapeTool::constrainedEnd(const QPointF &start, const QPointF &end,
                                  ShapeKind kind, bool constrain)
{
    if (!constrain)
        return end;

    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();

    if (kind == ShapeKind::Line) {
        // 吸附水平 / 垂直 / 45°
        const qreal angle = qAtan2(dy, dx);
        const qreal step = qDegreesToRadians(45.0);
        const qreal snapped = qRound(angle / step) * step;
        const qreal len = qSqrt(dx * dx + dy * dy);
        return QPointF(start.x() + len * qCos(snapped), start.y() + len * qSin(snapped));
    }

    const qreal side = qMin(qAbs(dx), qAbs(dy));
    const qreal sx = (dx < 0.0) ? -side : side;
    const qreal sy = (dy < 0.0) ? -side : side;
    return QPointF(start.x() + sx, start.y() + sy);
}

QRectF ShapeTool::currentImageRect() const
{
    return QRectF(m_startImage,
                  constrainedEnd(m_startImage, m_endImage, m_kind, m_constrain)).normalized();
}

QRectF ShapeTool::currentWidgetRect() const
{
    return QRectF(m_startWidget,
                  constrainedEnd(m_startWidget, m_endWidget, m_kind, m_constrain)).normalized();
}

void ShapeTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    Q_UNUSED(ctx)
    if (!m_dragging)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (m_kind == ShapeKind::Line) {
        const QPointF a = m_startWidget;
        const QPointF b = constrainedEnd(m_startWidget, m_endWidget, m_kind, m_constrain);
        painter.drawLine(a, b);
        pen.setColor(Qt::black);
        pen.setDashOffset(4);
        painter.setPen(pen);
        painter.drawLine(a, b);
    } else {
        const QRectF r = currentWidgetRect();
        if (m_kind == ShapeKind::Ellipse)
            painter.drawEllipse(r);
        else if (m_kind == ShapeKind::Triangle) {
            QPainterPath path;
            path.moveTo(r.center().x(), r.top());
            path.lineTo(r.bottomRight());
            path.lineTo(r.bottomLeft());
            path.closeSubpath();
            painter.drawPath(path);
        } else {
            painter.drawRect(r);
        }
        pen.setColor(Qt::black);
        pen.setDashOffset(4);
        painter.setPen(pen);
        if (m_kind == ShapeKind::Ellipse)
            painter.drawEllipse(r.adjusted(1, 1, -1, -1));
        else if (m_kind == ShapeKind::Triangle) {
            QPainterPath path;
            const QRectF ri = r.adjusted(1, 1, -1, -1);
            path.moveTo(ri.center().x(), ri.top());
            path.lineTo(ri.bottomRight());
            path.lineTo(ri.bottomLeft());
            path.closeSubpath();
            painter.drawPath(path);
        } else {
            painter.drawRect(r.adjusted(1, 1, -1, -1));
        }
    }
    painter.restore();
}

bool ShapeTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;
    m_dragging = true;
    m_constrain = event.modifiers.testFlag(Qt::ShiftModifier);
    m_startImage = event.imagePos;
    m_endImage = event.imagePos;
    m_startWidget = event.widgetPos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool ShapeTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging || !(event.buttons & Qt::LeftButton))
        return false;
    m_constrain = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool ShapeTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    m_constrain = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    m_dragging = false;
    emit repaintRequested();

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer || !layer->isVisible())
        return true;

    const QPointF endImg = constrainedEnd(m_startImage, m_endImage, m_kind, m_constrain);
    QRectF imageRect;
    if (m_kind == ShapeKind::Line) {
        imageRect = QRectF(m_startImage, endImg); // 保留端点方向
        if ((endImg - m_startImage).manhattanLength() < 1.0)
            return true;
    } else {
        imageRect = QRectF(m_startImage, endImg).normalized();
        if (imageRect.width() < 1.0 || imageRect.height() < 1.0)
            return true;
    }

    // 选项：填充/描边；直线强制描边
    bool fill = ctx.shapeFill;
    bool stroke = ctx.shapeStroke;
    if (m_kind == ShapeKind::Line) {
        fill = false;
        stroke = true;
    } else if (!fill && !stroke) {
        fill = true; // 都关时回退前景填充，避免空手势
    }

    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(), undoLabel());

    const QRectF localRect(
        layer->toLayerLocal(imageRect.topLeft()),
        layer->toLayerLocal(imageRect.bottomRight()));
    // 直线：用非 normalized 的端点矩形传给算子
    QRectF opRect = localRect;
    if (m_kind == ShapeKind::Line) {
        const QPointF a = layer->toLayerLocal(m_startImage);
        const QPointF b = layer->toLayerLocal(endImg);
        opRect = QRectF(a, b);
    }

    const auto clip = selectionClipFor(layer, ctx.document);
    const QRect dirtyLocal = PaintEngine::fillShape(
        layer->tiles(), m_kind, opRect, ctx.foreground,
        fill, stroke, ctx.shapeStrokeWidth, ctx.shapeCornerRadius, ctx.shapeAntialias, clip);
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

void ShapeTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return;
    m_dragging = false;
    emit repaintRequested();
}

} // namespace Ps
