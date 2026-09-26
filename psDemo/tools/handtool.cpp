#include "handtool.h"

namespace Ps {

HandTool::HandTool(QObject *parent)
    : Tool(Ps::ToolId::Hand, parent)
{
}

Qt::CursorShape HandTool::cursorShape() const
{
    return m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}

bool HandTool::isPanGesture(const ToolEvent &event)
{
    // 中键拖拽与 Alt+左键拖拽是 PS/GIMP 通用的临时平移手势，任何工具下都生效
    return event.button == Qt::MiddleButton || event.isAltLeft();
}

bool HandTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    if (!event.isLeft() && !event.isMiddle())
        return false;

    m_panning = true;
    m_lastWidgetPos = event.widgetPos;
    emit cursorChangeRequested(Qt::ClosedHandCursor);
    Q_UNUSED(view)
    return true;
}

bool HandTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    if (!m_panning)
        return false;
    // 只在左键/中键仍按下时平移，避免按键中途释放导致「粘住」
    if (!(event.buttons & (Qt::LeftButton | Qt::MiddleButton)))
        return false;

    const QPointF delta = event.widgetPos - m_lastWidgetPos;
    m_lastWidgetPos = event.widgetPos;
    view.panBy(delta);
    return true;
}

bool HandTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_panning)
        return false;

    m_panning = false;
    emit cursorChangeRequested(Qt::OpenHandCursor);
    // 中键/左键都可能是结束平移的那一次
    return event.isLeft() || event.isMiddle();
}

void HandTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    // 关键：切走工具时必须清掉拖拽中间态，否则平移会「粘住」
    m_panning = false;
}

} // namespace Ps
