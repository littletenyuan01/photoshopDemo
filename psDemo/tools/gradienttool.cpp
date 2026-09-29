#include "gradienttool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "toolcursor.h"

#include <QLineF>
#include <QPen>

namespace Ps {

GradientTool::GradientTool(QObject *parent)
    : Tool(Ps::ToolId::Gradient, parent)
{
}

QCursor GradientTool::cursor() const
{
    return ToolCursor::gradientStyle();
}

bool GradientTool::hasOverlay() const
{
    return m_dragging;
}

void GradientTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    Q_UNUSED(ctx)
    if (!m_dragging)
        return;

    // 控件坐标画拖拽预览线（对齐 PS 渐变拖拽橡皮筋）
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(255, 255, 255, 220), 1.0);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawLine(m_startWidget, m_endWidget);

    // 黑描边增强对比（深色/浅色画布都能看见）
    pen.setColor(QColor(0, 0, 0, 180));
    painter.setPen(pen);
    painter.drawLine(m_startWidget + QPointF(1, 1), m_endWidget + QPointF(1, 1));

    // 起止小十字
    const qreal arm = 4.0;
    auto drawCross = [&](const QPointF &p) {
        painter.drawLine(QPointF(p.x() - arm, p.y()), QPointF(p.x() + arm, p.y()));
        painter.drawLine(QPointF(p.x(), p.y() - arm), QPointF(p.x(), p.y() + arm));
    };
    pen.setColor(QColor(255, 255, 255, 230));
    painter.setPen(pen);
    drawCross(m_startWidget);
    drawCross(m_endWidget);
    painter.restore();
}

bool GradientTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    m_dragging = true;
    m_startImage = event.imagePos;
    m_endImage = event.imagePos;
    m_startWidget = event.widgetPos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool GradientTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool GradientTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    m_dragging = false;
    emit repaintRequested(); // 清掉浮层

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    // 对照梯度工具：隐藏层不可改（GIMP 另有 edit_non_visible）
    if (!layer || !layer->isVisible())
        return true;

    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(),
                                      QObject::tr("渐变"));

    const PaintEngine::GradientType type = ctx.gradientType;
    // 对照 gimp_item_mask_intersect：渐变只写入选区内
    PaintEngine::SelectionClip clip;
    clip.selection = &ctx.document->selection();
    clip.layerOffsetX = layer->offsetX();
    clip.layerOffsetY = layer->offsetY();

    // 对应 gimp_drawable_gradient(..., start, end, ...)；坐标换到层内
    const QRect dirtyLocal = PaintEngine::applyGradient(
        layer->tiles(),
        layer->toLayerLocal(m_startImage),
        layer->toLayerLocal(m_endImage),
        ctx.foreground,
        ctx.background,
        type,
        ctx.gradientOpacity,
        ctx.gradientOffsetPercent,
        ctx.gradientReverse,
        ctx.gradientDither,
        clip);

    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

void GradientTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return;
    m_dragging = false;
    emit repaintRequested();
}

} // namespace Ps
