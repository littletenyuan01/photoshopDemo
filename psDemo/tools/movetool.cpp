#include "movetool.h"

namespace Ps {

MoveTool::MoveTool(QObject *parent)
    : Tool(Ps::ToolId::Move, parent)
{
}

QString MoveTool::displayName() const
{
    return tr("移动工具");
}

QString MoveTool::hint() const
{
    return tr("该工具逻辑尚未接入（见 wiki/Roadmap.md）");
}

bool MoveTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(event)
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    return false; // 不消费 → 画布不会因点击而改变
}

} // namespace Ps
