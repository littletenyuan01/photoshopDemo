/**
 * toolmanager.cpp — 工具注册表与事件分发实现（tools 层）。
 *
 * 构造时注册全部工具；未接入逻辑的工具回退到 MoveTool。
 */
#include "toolmanager.h"

#include "clonestamptool.h"
#include "croptool.h"
#include "eyedroppertool.h"
#include "focustool.h"
#include "gradienttool.h"
#include "handtool.h"
#include "lassotool.h"
#include "magicwandtool.h"
#include "magneticlassotool.h"
#include "marqueeselecttool.h"
#include "movetool.h"
#include "paintbuckettool.h"
#include "painttool.h"
#include "polygonallassotool.h"
#include "quickselecttool.h"
#include "shapetool.h"
#include "tonetool.h"
#include "tool.h"
#include "transformtool.h"
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
    registerTool(std::make_unique<TransformTool>());
    registerTool(std::make_unique<MarqueeSelectTool>(MarqueeSelectTool::Shape::Rect));
    registerTool(std::make_unique<MarqueeSelectTool>(MarqueeSelectTool::Shape::Ellipse));
    registerTool(std::make_unique<LassoTool>());
    registerTool(std::make_unique<PolygonalLassoTool>());
    registerTool(std::make_unique<MagneticLassoTool>());
    registerTool(std::make_unique<MagicWandTool>());
    registerTool(std::make_unique<QuickSelectTool>());
    registerTool(std::make_unique<CropTool>());
    registerTool(std::make_unique<EyedropperTool>());
    registerTool(std::make_unique<PaintTool>(Ps::ToolId::Brush, /*eraseMode=*/false));
    registerTool(std::make_unique<PaintTool>(Ps::ToolId::Pencil, /*eraseMode=*/false,
                                             /*hardness=*/1.0));
    registerTool(std::make_unique<CloneStampTool>());
    registerTool(std::make_unique<PaintTool>(Ps::ToolId::Eraser, /*eraseMode=*/true));
    registerTool(std::make_unique<PaintBucketTool>());
    registerTool(std::make_unique<GradientTool>());
    registerTool(std::make_unique<FocusTool>(Ps::ToolId::Blur, FocusMode::Blur));
    registerTool(std::make_unique<FocusTool>(Ps::ToolId::Sharpen, FocusMode::Sharpen));
    registerTool(std::make_unique<FocusTool>(Ps::ToolId::Smudge, FocusMode::Smudge));
    registerTool(std::make_unique<ToneTool>(Ps::ToolId::Dodge, ToneMode::Dodge));
    registerTool(std::make_unique<ToneTool>(Ps::ToolId::Sponge, ToneMode::Sponge));
    registerTool(std::make_unique<ShapeTool>(Ps::ToolId::ShapeRect, ShapeKind::Rect));
    registerTool(std::make_unique<ShapeTool>(Ps::ToolId::ShapeEllipse, ShapeKind::Ellipse));
    registerTool(std::make_unique<ShapeTool>(Ps::ToolId::ShapeTriangle, ShapeKind::Triangle));
    registerTool(std::make_unique<ShapeTool>(Ps::ToolId::ShapeLine, ShapeKind::Line));
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
    connect(m_activeTool, &Tool::foregroundPicked,
            this, &ToolManager::foregroundPicked);
    connect(m_activeTool, &Tool::backgroundPicked,
            this, &ToolManager::backgroundPicked);
    connect(m_activeTool, &Tool::liveProjectionCommitted,
            this, &ToolManager::liveProjectionCommitted);
}

void ToolManager::setContext(const ToolContext &ctx)
{
    // 上下文由管理器持有，每次事件分发时按值传给活动工具（工具不留副本）
    m_context = ctx;
}

bool ToolManager::beginTemporaryTool(Ps::ToolId id)
{
    // 不可嵌套；已是目标工具则无需切换
    if (m_toolBeforeTemporary)
        return false;
    Tool *next = tool(id);
    if (!next || next == m_activeTool)
        return false;

    m_toolBeforeTemporary = m_activeTool;
    m_activeTool = next;
    rewriteConnections();
    // 故意不 emit activeToolChanged：工具箱/选项栏仍显示用户选定的原工具（对照 PS）
    emit cursorChangeRequested(activeCursor());
    return true;
}

void ToolManager::endTemporaryTool()
{
    if (!m_toolBeforeTemporary)
        return;
    m_activeTool = m_toolBeforeTemporary;
    m_toolBeforeTemporary = nullptr;
    rewriteConnections();
    emit cursorChangeRequested(activeCursor());
}

bool ToolManager::setActiveTool(Ps::ToolId id, ViewPort &view)
{
    // 用户从工具箱显式切工具：取消空格临时态，再从「压栈前工具」正常切换
    if (m_toolBeforeTemporary) {
        if (m_activeTool && m_activeTool != m_toolBeforeTemporary)
            m_activeTool->deactivate(m_context, view); // 清临时抓手拖拽态
        m_activeTool = m_toolBeforeTemporary;
        m_toolBeforeTemporary = nullptr;
    }

    Tool *next = tool(id);
    if (!next) {
        // 未接入逻辑的工具 → 兜底到 MoveTool，而不是保留上一个工具的行为
        next = m_tools.value(static_cast<int>(Ps::ToolId::Move), nullptr);
    }
    if (next == m_activeTool) {
        // 已是该工具：仍 activate（Ctrl+T 重复进入时重启自由变换会话）
        next->activate(m_context, view);
        emit cursorChangeRequested(activeCursor());
        return false;
    }

    if (m_activeTool)
        m_activeTool->deactivate(m_context, view); // 清理拖拽中间态，避免状态「粘住」

    m_activeTool = next;

    rewriteConnections();
    m_activeTool->activate(m_context, view);
    emit activeToolChanged(activeToolId());
    emit cursorChangeRequested(activeCursor());
    return true;
}

bool ToolManager::dispatchPress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    m_context = ctx; // 与画布侧保持一致
    return m_activeTool->mousePress(event, ctx, view);
}

bool ToolManager::dispatchMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    m_context = ctx;
    return m_activeTool->mouseMove(event, ctx, view);
}

bool ToolManager::dispatchRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    m_context = ctx;
    return m_activeTool->mouseRelease(event, ctx, view);
}

bool ToolManager::dispatchKeyPress(int key, Qt::KeyboardModifiers modifiers,
                                   const ToolContext &ctx, ViewPort &view)
{
    if (!m_activeTool)
        return false;
    m_context = ctx;
    return m_activeTool->keyPress(key, modifiers, ctx, view);
}

bool ToolManager::wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const
{
    return m_activeTool && m_activeTool->wantsShortcutOverride(key, modifiers);
}

QCursor ToolManager::activeCursor() const
{
    return m_activeTool ? m_activeTool->cursor() : QCursor(Qt::ArrowCursor);
}

} // namespace Ps
