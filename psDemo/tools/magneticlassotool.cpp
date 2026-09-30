/**
 * magneticlassotool.cpp — MagneticLassoTool 实现（tools 层）。
 *
 * 按下时 Compositor::composite 缓存源图；拖拽中 MagneticEdgeSnap::snap；
 * 松手 selectPolygon → SelectPolygonOp。
 */
#include "magneticlassotool.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "engine/magneticedgesnap.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

namespace Ps {

namespace {
constexpr qreal kMinSampleDist = 2.5;
}

MagneticLassoTool::MagneticLassoTool(QObject *parent)
    : Tool(Ps::ToolId::MagneticLasso, parent)
{
}

Qt::CursorShape MagneticLassoTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool MagneticLassoTool::hasOverlay() const
{
    return m_dragging && m_imagePts.size() >= 2;
}

ChannelOp MagneticLassoTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
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

void MagneticLassoTool::clearStroke()
{
    m_dragging = false;
    m_source = QImage();
    m_imagePts.clear();
}

void MagneticLassoTool::appendSnapped(const QPointF &imagePos)
{
    const QPointF snapped = MagneticEdgeSnap::snap(m_source, imagePos, m_searchRadius);
    if (!m_imagePts.isEmpty()) {
        const QPointF d = snapped - m_imagePts.last();
        if (d.x() * d.x() + d.y() * d.y() < kMinSampleDist * kMinSampleDist)
            return;
    }
    m_imagePts.append(snapped);
}

void MagneticLassoTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (m_imagePts.size() < 2)
        return;

    // 文档坐标 → 控件坐标（ToolContext 已带 viewZoom/offset）
    QVector<QPointF> widgetPts;
    widgetPts.reserve(m_imagePts.size());
    for (const QPointF &p : m_imagePts)
        widgetPts.append(ctx.imageToWidget(p));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto stroke = [&](const QColor &color, qreal dashOffset) {
        QPen pen(color);
        pen.setCosmetic(true);
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({4, 4});
        pen.setDashOffset(dashOffset);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(widgetPts.constData(), widgetPts.size());
        if (widgetPts.size() >= 3)
            painter.drawLine(widgetPts.last(), widgetPts.first());
    };
    stroke(Qt::white, 0);
    stroke(QColor(0, 200, 255), 4); // 略区分自由套索的黑白虚线

    painter.restore();
}

bool MagneticLassoTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    // 对照 iscissors 用 pickable 缓冲：此处用文档合成图作边缘源
    m_source = Compositor::composite(*ctx.document);
    if (m_source.isNull())
        return false;
    if (m_source.format() != QImage::Format_ARGB32_Premultiplied)
        m_source = m_source.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    m_dragging = true;
    m_op = opFromModifiers(event.modifiers);
    m_imagePts.clear();
    appendSnapped(event.imagePos);
    emit repaintRequested();
    return true;
}

bool MagneticLassoTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    appendSnapped(event.imagePos);
    emit repaintRequested();
    return true;
}

bool MagneticLassoTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    appendSnapped(event.imagePos);
    m_dragging = false;
    emit repaintRequested();

    const QVector<QPointF> pts = m_imagePts;
    const ChannelOp op = m_op;
    clearStroke();

    if (!ctx.document || pts.size() < 3)
        return true;

    const QPolygonF poly(pts);
    const QRectF br = poly.boundingRect();
    if (br.width() < 0.5 && br.height() < 0.5)
        return true;

    ctx.document->selectPolygon(poly, op);
    return true;
}

void MagneticLassoTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging && m_imagePts.isEmpty())
        return;
    clearStroke();
    emit repaintRequested();
}

} // namespace Ps
