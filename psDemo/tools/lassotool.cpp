/**
 * lassotool.cpp — LassoTool 实现（tools 层）。
 *
 * 【对照 GIMP】gimpfreeselecttool.c → gimp_channel_select_polygon；
 * 本项目交互对齐 PS 自由套索（拖拽手绘、松手闭合），写入走算子框架。
 */
#include "lassotool.h"

#include "domain/imagedocument.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

namespace Ps {

namespace {
/** 图像坐标最小采样间距（像素）；过密点浪费且无助于轮廓。 */
constexpr qreal kMinSampleDist = 1.5;
}

LassoTool::LassoTool(QObject *parent)
    : Tool(Ps::ToolId::Lasso, parent)
{
}

Qt::CursorShape LassoTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool LassoTool::hasOverlay() const
{
    return m_dragging && !m_widgetPts.isEmpty();
}

ChannelOp LassoTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
    // 与 MarqueeSelectTool 一致：press 时锁定运算模式
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

void LassoTool::appendPoint(const QPointF &imagePos, const QPointF &widgetPos)
{
    if (!m_imagePts.isEmpty()) {
        const QPointF d = imagePos - m_imagePts.last();
        if (d.x() * d.x() + d.y() * d.y() < kMinSampleDist * kMinSampleDist)
            return;
    }
    m_imagePts.append(imagePos);
    m_widgetPts.append(widgetPos);
}

void LassoTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    Q_UNUSED(ctx)
    if (m_widgetPts.size() < 2)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(Qt::white);
    pen.setCosmetic(true);
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({4, 4});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(m_widgetPts.constData(), m_widgetPts.size());
    // 首尾虚线预览闭合（正式 mask 由松手时多边形填充）
    if (m_widgetPts.size() >= 3)
        painter.drawLine(m_widgetPts.last(), m_widgetPts.first());

    pen.setColor(Qt::black);
    pen.setDashOffset(4);
    painter.setPen(pen);
    painter.drawPolyline(m_widgetPts.constData(), m_widgetPts.size());
    if (m_widgetPts.size() >= 3)
        painter.drawLine(m_widgetPts.last(), m_widgetPts.first());

    painter.restore();
}

bool LassoTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    m_dragging = true;
    m_op = opFromModifiers(event.modifiers);
    m_imagePts.clear();
    m_widgetPts.clear();
    appendPoint(event.imagePos, event.widgetPos);
    emit repaintRequested();
    return true;
}

bool LassoTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    appendPoint(event.imagePos, event.widgetPos);
    emit repaintRequested();
    return true;
}

bool LassoTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    appendPoint(event.imagePos, event.widgetPos);
    m_dragging = false;
    emit repaintRequested();

    if (!ctx.document)
        return true;

    // 对照 gimp_free_select_tool_select：n_points > 2 才提交
    if (m_imagePts.size() < 3) {
        m_imagePts.clear();
        m_widgetPts.clear();
        return true;
    }

    const QPolygonF poly(m_imagePts);
    const QRectF br = poly.boundingRect();
    // 退化路径（几乎零面积）不提交，避免误 Replace 清空
    if (br.width() < 0.5 && br.height() < 0.5) {
        m_imagePts.clear();
        m_widgetPts.clear();
        return true;
    }

    ctx.document->selectPolygon(poly, m_op);
    m_imagePts.clear();
    m_widgetPts.clear();
    return true;
}

void LassoTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging && m_imagePts.isEmpty())
        return;
    m_dragging = false;
    m_imagePts.clear();
    m_widgetPts.clear();
    emit repaintRequested();
}

} // namespace Ps
