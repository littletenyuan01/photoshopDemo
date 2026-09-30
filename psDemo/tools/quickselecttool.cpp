/**
 * quickselecttool.cpp — QuickSelectTool 实现（tools 层）。
 */
#include "quickselecttool.h"

#include "domain/imagedocument.h"
#include "toolcursor.h"

#include <QtMath>

namespace Ps {

namespace {
constexpr qreal kSampleSpacingFactor = 0.6; // 相对笔刷半径
}

QuickSelectTool::QuickSelectTool(QObject *parent)
    : Tool(Ps::ToolId::QuickSelect, parent)
{
}

QCursor QuickSelectTool::cursor() const
{
    return ToolCursor::fromToolIcon(QStringLiteral(":/icons/tools/quick-select.png"),
                                    0.5, 0.5, 28);
}

ChannelOp QuickSelectTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (shift && ctrl)
        return ChannelOp::Intersect;
    if (ctrl)
        return ChannelOp::Subtract;
    if (shift)
        return ChannelOp::Add;
    // 无修饰：首次 Replace，后续拖拽用 Add 扩张（对齐 PS 快速选择习惯）
    return ChannelOp::Replace;
}

void QuickSelectTool::sampleAt(const ToolContext &ctx, const QPointF &imagePos)
{
    if (!ctx.document)
        return;

    const QPoint seed(qFloor(imagePos.x()), qFloor(imagePos.y()));
    if (seed.x() < 0 || seed.y() < 0
        || seed.x() >= ctx.document->width() || seed.y() >= ctx.document->height())
        return;

    // 快速选择始终按「连续」扩张；容差 / 取样来自选项栏
    ctx.document->selectFlood(seed,
                              ctx.selTolerance,
                              /*contiguous=*/true,
                              ctx.selSampleMerged,
                              m_op);
}

bool QuickSelectTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    m_dragging = true;
    m_op = opFromModifiers(event.modifiers);
    // 无修饰时首击 Replace，之后移动改为 Add（扩张）
    sampleAt(ctx, event.imagePos);
    if (m_op == ChannelOp::Replace)
        m_op = ChannelOp::Add;
    m_lastSample = event.imagePos;
    return true;
}

bool QuickSelectTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    const qreal spacing = qMax(2.0, ctx.brushRadius * kSampleSpacingFactor);
    const QPointF d = event.imagePos - m_lastSample;
    if (d.x() * d.x() + d.y() * d.y() < spacing * spacing)
        return true;

    sampleAt(ctx, event.imagePos);
    m_lastSample = event.imagePos;
    return true;
}

bool QuickSelectTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(event)
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    m_dragging = false;
    return true;
}

void QuickSelectTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_dragging = false;
}

} // namespace Ps
