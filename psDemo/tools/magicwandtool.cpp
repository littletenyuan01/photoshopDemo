/**
 * magicwandtool.cpp — MagicWandTool 实现（tools 层）。
 */
#include "magicwandtool.h"

#include "domain/imagedocument.h"
#include "domain/selection.h"
#include "toolcursor.h"

#include <QtMath>

namespace Ps {

MagicWandTool::MagicWandTool(QObject *parent)
    : Tool(Ps::ToolId::MagicWand, parent)
{
}

QCursor MagicWandTool::cursor() const
{
    return ToolCursor::fromToolIcon(QStringLiteral(":/icons/tools/magic-wand.png"),
                                    0.25, 0.85, 28);
}

ChannelOp MagicWandTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
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

bool MagicWandTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    const QPoint seed(qFloor(event.imagePos.x()), qFloor(event.imagePos.y()));
    if (seed.x() < 0 || seed.y() < 0
        || seed.x() >= ctx.document->width() || seed.y() >= ctx.document->height())
        return false;

    ctx.document->selectFlood(seed,
                              ctx.selTolerance,
                              ctx.selContiguous,
                              ctx.selSampleMerged,
                              opFromModifiers(event.modifiers));
    return true;
}

} // namespace Ps
