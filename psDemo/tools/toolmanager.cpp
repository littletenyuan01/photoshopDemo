#include "toolmanager.h"

#include "handtool.h"
#include "movetool.h"
#include "painttool.h"
#include "tool.h"
#include "zoomtool.h"

#include <memory>

namespace Ps {

ToolManager::ToolManager(QObject *parent)
    : QObject(parent)
{
    // —— 工具注册表 ——
    // 新增工具只需在此加一行；CanvasView / MainWindow 无需改动。
    // 【对照 GIMP】等价于 app/tools/tools-enums.c + gimp_tool_info_new 的注册表角色。
    registerTool(std::make_unique<MoveTool>());
    registerTool(std::make_unique<PaintTool>(Ps::ToolId::Brush, /*eraseMode=*/false));
    registerTool(std::make_unique<PaintTool>(Ps::ToolId::Eraser, /*eraseMode=*/true));
    registerTool(std::make_unique<HandTool>());
    registerTool(std::make_unique<ZoomTool>());

    // 兜底：MoveTool 不消费事件，未接入逻辑的工具切过去等同「什么都不做」
    m_activeTool = m_tools.value(static_cast<int>(Ps::ToolId::Move), nullptr);
    rewriteConnections();
}

ToolManager::~ToolManager()
{
    // 工具是 QObject 但所有权由本类持有（未指定 parent），故手动释放
    qDeleteAll(m_tools);
    m_tools.clear();
}

void ToolManager::registerTool(std::unique_ptr<Tool> tool)
{
    if (!tool)
        return;
    Tool *raw = tool.release(); // 所有权转入 m_tools
    m_tools.insert(static_cast<int>(raw->id()), raw);
}

Tool *ToolManager::tool(Ps::ToolId id) const
{
    return m_tools.value(static_cast<int>(id), nullptr);
}

Ps::ToolId ToolManager::activeToolId() const
{
    return m_activeTool ? m_activeTool->id() : Ps::ToolId::Move;
}

void ToolManager::rewriteConnections()
{
    // 断开全部旧的「工具 → 管理器」连接，只保留管理器的对外信号连接。
    // 用 disconnect(sender=nullptr) 会误伤外部连接，故逐个工具断。
    for (Tool *t : std::as_const(m_tools)) {
        disconnect(t, nullptr, this, nullptr);
    }
    if (!m_activeTool)
        return;

    connect(m_activeTool, &Tool::repaintRequested,
            this, &ToolManager::repaintRequested);
    connect(m_activeTool, &Tool::cursorChangeRequested,
            this, &ToolManager::cursorChangeRequested);
}

void ToolManager::setContext(const ToolContext &ctx)
{
    // 上下文由管理器持有，每次事件分发时按值传给活动工具（工具不留副本）
    m_context = ctx;
}

bool ToolManager::setActiveTool(Ps::ToolId id, ViewPort &view)
{
    Tool *next = tool(id);
    if (!next) {
        // 未接入逻辑的工具 → 兜底到 MoveTool，而不是保留上一个工具的行为
        next = m_tools.value(static_cast<int>(Ps::ToolId::Move), nullptr);
    }
    if (next == m_activeTool)
        return false;

    if (m_activeTool)
        m_activeTool->deactivate(m_context, view); // 清理拖拽中间态，避免状态「粘住」

    m_activeTool = next;

    rewriteConnections();
    emit activeToolChanged(activeToolId());
    emit cursorChangeRequested(activeCursorShape());
    return true;
}

bool ToolManager::dispatchPress(const ToolEvent &event, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    return m_activeTool->mousePress(event, m_context, view);
}

bool ToolManager::dispatchMove(const ToolEvent &event, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    return m_activeTool->mouseMove(event, m_context, view);
}

bool ToolManager::dispatchRelease(const ToolEvent &event, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    return m_activeTool->mouseRelease(event, m_context, view);
}

Qt::CursorShape ToolManager::activeCursorShape() const
{
    return m_activeTool ? m_activeTool->cursorShape() : Qt::ArrowCursor;
}

} // namespace Ps
