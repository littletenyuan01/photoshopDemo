#ifndef TOOLMANAGER_H
#define TOOLMANAGER_H

#include "toolcontext.h"
#include "toolevent.h"
#include "toolid.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace Ps {

class Tool;
class ViewPort;

/**
 * 工具管理器（tools 层）：持有全部工具实例，按 id 分派事件。
 *
 * 【对照 GIMP】app/tools/gimptoolmanager.c 的瘦身版：
 * GIMP 按 GimpContext 激活工具、按 GimpDisplay 为每个显示维护工具状态；
 * 本项目简化为「单文档 + 单活动工具」。
 *
 * 【用法】CanvasView 把事件原样转发进来，不关心里面有哪些工具：
 *
 *   m_toolManager->dispatchPress(event, m_toolContext, *this);
 *
 * 新增工具 = movetool/handtool 这样加一个类 + 在 ToolManager 构造函数注册一行，
 * **CanvasView 与 MainWindow 都不用改**。
 *
 * 【信号转发】活动工具会随用户切换而变，CanvasView 无法预先 connect 到具体工具。
 * 因此由本类持有「当前活动工具」的信号连接，并原样向上转发（见 rewriteConnections）。
 */
class ToolManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolManager(QObject *parent = nullptr);
    ~ToolManager() override;

    /** 取工具；未注册返回 nullptr。 */
    Tool *tool(Ps::ToolId id) const;

    /** 当前活动工具；恒不为空（构造时已回退到 MoveTool）。 */
    Tool *activeTool() const { return m_activeTool; }
    Ps::ToolId activeToolId() const;

    /**
     * 更新全局上下文（颜色/笔刷/文档）。
     * 颜色或笔刷改变时必须调用，否则活动工具用的是过期参数。
     */
    void setContext(const ToolContext &ctx);

    /** 切换活动工具；会 deactivate 旧工具。返回是否真的换了。 */
    bool setActiveTool(Ps::ToolId id, ViewPort &view);

    // —— 事件分发：返回 true 表示已被工具消费 ——
    bool dispatchPress(const ToolEvent &event, ViewPort &view);
    bool dispatchMove(const ToolEvent &event, ViewPort &view);
    bool dispatchRelease(const ToolEvent &event, ViewPort &view);

    /** 当前活动工具的光标形状。 */
    Qt::CursorShape activeCursorShape() const;

signals:
    /** 活动工具变了（选项栏/状态栏用）。 */
    void activeToolChanged(Ps::ToolId id);
    /** 活动工具请求重绘（透传自 Tool::repaintRequested）。 */
    void repaintRequested();
    /** 活动工具请求改光标（透传）。 */
    void cursorChangeRequested(Qt::CursorShape shape);

private:
    /** 注册工具，key 取自 tool->id()。 */
    void registerTool(std::unique_ptr<Tool> tool);

    /** 把活动工具的信号接到本类的转发信号上（切工具时重来一遍）。 */
    void rewriteConnections();

    QHash<int, Tool *> m_tools; ///< 拥有所有权（析构时统一 delete）
    Tool *m_activeTool = nullptr;
    ToolContext m_context;
};

} // namespace Ps

#endif // TOOLMANAGER_H
