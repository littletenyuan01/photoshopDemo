#include "zoomtool.h"

namespace Ps {

ZoomTool::ZoomTool(QObject *parent)
    : Tool(Ps::ToolId::Zoom, parent)
{
}

QString ZoomTool::displayName() const
{
    return tr("缩放工具");
}

QString ZoomTool::hint() const
{
    return tr("左键放大，右键缩小；Alt+左键临时平移");
}

Qt::CursorShape ZoomTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool ZoomTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)

    // 左键放大 / 右键缩小 —— 早先这段在 CanvasView 里写了两遍，现已收敛为一处
    if (event.isLeft()) {
        view.zoomAt(event.widgetPos, kStep);
        return true;
    }
    if (event.isRight()) {
        view.zoomAt(event.widgetPos, 1.0 / kStep);
        return true;
    }
    return false;
}

} // namespace Ps
