/**
 * marqueeselecttool.cpp — 矩形/椭圆选框工具实现（tools 层）。
 */
#include "marqueeselecttool.h"

#include "domain/imagedocument.h"
#include "domain/selection.h"

#include <QPen>
#include <QtMath>

namespace Ps {

MarqueeSelectTool::MarqueeSelectTool(Shape shape, QObject *parent)
    : Tool(shape == Shape::Ellipse ? Ps::ToolId::EllipseSelect : Ps::ToolId::RectSelect, parent)
    , m_shape(shape)
{
}

Qt::CursorShape MarqueeSelectTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool MarqueeSelectTool::hasOverlay() const
{
    return m_dragging;
}

ChannelOp MarqueeSelectTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
    // 对照 GIMP/PS：Shift 加选、Ctrl 减选、二者相交（本项目在 press 时锁定）
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (shift && ctrl)
        return ChannelOp::Intersect;
    if (shift)
        return ChannelOp::Add;
    if (ctrl)
        return ChannelOp::Subtract;
    return ChannelOp::Replace;
}

QPointF MarqueeSelectTool::constrainedEnd(const QPointF &start, const QPointF &end, bool constrain)
{
    if (!constrain)
        return end;

    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    const qreal side = qMin(qAbs(dx), qAbs(dy));
    const qreal sx = (dx < 0.0) ? -side : side;
    const qreal sy = (dy < 0.0) ? -side : side;
    return QPointF(start.x() + sx, start.y() + sy);
}

QRectF MarqueeSelectTool::currentImageRect() const
{
    return QRectF(m_startImage, constrainedEnd(m_startImage, m_endImage, m_constrain)).normalized();
}

QRectF MarqueeSelectTool::currentWidgetRect() const
{
    return QRectF(m_startWidget, constrainedEnd(m_startWidget, m_endWidget, m_constrain)).normalized();
}

void MarqueeSelectTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    Q_UNUSED(ctx)
    if (!m_dragging)
        return;

    const QRectF r = currentWidgetRect();
    if (r.width() < 0.5 && r.height() < 0.5)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, m_shape == Shape::Ellipse);

    // 黑白虚线橡皮筋（正式选区由 CanvasView 根据 mask 轮廓画蚂蚁线）
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({4, 4});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (m_shape == Shape::Ellipse)
        painter.drawEllipse(r);
    else
        painter.drawRect(r);

    pen.setColor(Qt::black);
    pen.setDashOffset(4);
    painter.setPen(pen);
    const QRectF inner = r.adjusted(1, 1, -1, -1);
    if (m_shape == Shape::Ellipse)
        painter.drawEllipse(inner);
    else
        painter.drawRect(inner);
    painter.restore();
}

bool MarqueeSelectTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    m_dragging = true;
    m_op = opFromModifiers(event.modifiers);
    // 按下时的 Shift 已用于加选；拖中再按住 Shift 才约束为正方形/正圆（对齐 PS）
    m_constrain = false;
    m_startImage = event.imagePos;
    m_endImage = event.imagePos;
    m_startWidget = event.widgetPos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool MarqueeSelectTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    m_constrain = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool MarqueeSelectTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    m_constrain = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    m_dragging = false;
    emit repaintRequested();

    if (!ctx.document)
        return true;

    const QRectF rf = currentImageRect();
    QRect rect(qFloor(rf.left()), qFloor(rf.top()),
               qCeil(rf.right()) - qFloor(rf.left()),
               qCeil(rf.bottom()) - qFloor(rf.top()));
    // 零面积不提交（避免单击误清空；清空请用 Ctrl+D）
    if (rect.width() < 1 || rect.height() < 1)
        return true;

    if (m_shape == Shape::Ellipse)
        ctx.document->selectEllipse(rect, m_op);
    else
        ctx.document->selectRectangle(rect, m_op);
    return true;
}

void MarqueeSelectTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return;
    m_dragging = false;
    m_constrain = false;
    emit repaintRequested();
}

} // namespace Ps
