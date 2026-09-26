#include "tool.h"

#include "domain/imagedocument.h"

namespace Ps {

Tool::Tool(Ps::ToolId id, QObject *parent)
    : QObject(parent)
    , m_id(id)
{
}

Tool::~Tool() = default;

bool Tool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(event)
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    return false; // 默认不消费移动事件
}

bool Tool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(event)
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    return false;
}

void Tool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
}

void Tool::markDocumentDirty(const ToolContext &ctx, const QRect &rect)
{
    if (ctx.document)
        ctx.document->markDirty(rect);
    emit repaintRequested();
}

} // namespace Ps
