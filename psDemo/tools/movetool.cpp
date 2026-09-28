#include "movetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QtMath>

namespace Ps {

MoveTool::MoveTool(QObject *parent)
    : Tool(Ps::ToolId::Move, parent)
{
}

Qt::CursorShape MoveTool::cursorShape() const
{
    return Qt::SizeAllCursor;
}

bool MoveTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;
    if (!ctx.document)
        return false;

    // 对照 gimp_move_tool（!move_current）：先 gimp_image_pick_layer，再设为选中
    const int docX = qFloor(event.imagePos.x());
    const int docY = qFloor(event.imagePos.y());
    const int picked = ctx.document->pickLayerAt(docX, docY);
    if (picked >= 0)
        ctx.document->setActiveLayerIndex(picked);

    Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return false;

    m_dragging = true;
    m_lastImagePos = event.imagePos;
    return true;
}

bool MoveTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;
    if (!ctx.document)
        return false;

    const int index = ctx.document->activeLayerIndex();
    if (index < 0)
        return false;

    // 整像素步进（对照 GIMP SIGNED_ROUND 后的整数平移）
    const int dx = qRound(event.imagePos.x() - m_lastImagePos.x());
    const int dy = qRound(event.imagePos.y() - m_lastImagePos.y());
    if (dx == 0 && dy == 0)
        return true;

    ctx.document->translateLayer(index, dx, dy);
    m_lastImagePos += QPointF(dx, dy);
    emit repaintRequested();
    return true;
}

bool MoveTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    m_dragging = false;
    return event.isLeft();
}

void MoveTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_dragging = false;
}

} // namespace Ps
