#ifndef TOOL_H
#define TOOL_H

#include "toolcontext.h"
#include "toolevent.h"
#include "toolid.h"

#include <QCursor>
#include <QObject>
#include <QPainter>
#include <QString>

namespace Ps {

/**
 * 工具基类（tools 层）。
 *
 * 【对照 GIMP】app/tools/gimptool.c + gimptool.h 的瘦身版：
 * GIMP 的 GimpTool 是一台带 button_press / motion / button_release / cursor_update /
 * draw 虚函数的交互状态机，由 GimpToolManager 统一转发事件。本项目保留同样的形状，
 * 但去掉 GimpToolControl / 选项 / 撤销 extents 等机制。
 *
 * 【为什么必须抽它】
 * 早先工具逻辑全写在 CanvasView::mousePressEvent 的 if-else 链里，
 * 每加一个工具就要往里塞一个分支，工具数与 CanvasView 体积线性相关。
 * 现在新增工具 = 新增一个 Tool 子类 + 在 ToolManager 注册，
 * **CanvasView 一行都不用改**。
 *
 * 【契约】
 * - 事件返回 true 表示已消费（CanvasView 不再做默认处理）。
 * - 工具通过 signals 请求重绘；CanvasView 连接后统一 invalidate。
 * - 工具**不得**包含任何 UI 头文件（保持 tools 层不依赖 ui 层）。
 */
class Tool : public QObject
{
    Q_OBJECT

public:
    explicit Tool(Ps::ToolId id, QObject *parent = nullptr);
    ~Tool() override;

    Ps::ToolId id() const { return m_id; }

    /** 鼠标光标形状（CanvasView 在切换工具后查询）。 */
    virtual Qt::CursorShape cursorShape() const { return Qt::ArrowCursor; }

    /** 是否在画布上自绘（临时图形，如选框矩形）。 */
    virtual bool hasOverlay() const { return false; }
    /** 在画布已绘制图像之后叠加自绘内容；坐标已是控件坐标。 */
    virtual void drawOverlay(QPainter &painter, const ToolContext &ctx) const
    {
        Q_UNUSED(painter)
        Q_UNUSED(ctx)
    }

    // —— 事件入口：返回 true = 已消费 ——
    virtual bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) = 0;
    virtual bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view);
    virtual bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view);

    /** 工具被切走时调用（清理拖拽中间态）。 */
    virtual void deactivate(const ToolContext &ctx, ViewPort &view);

signals:
    /** 请求重绘画布。 */
    void repaintRequested();
    /** 光标形状变了，请求画布更新光标。 */
    void cursorChangeRequested(Qt::CursorShape shape);

protected:
    /**
     * 子类改完像素后调它：标记脏区 + 请求重绘。
     * 【为什么上下文要传进来】工具不保存 ToolContext 副本（早期版本存了一份 m_ctx，
     * 只有这里读得到，属于「只写不读」的冗余状态）；上下文由 ToolManager 在每次事件
     * 分发时按值给出，直接透传即可。
     */
    void markDocumentDirty(const ToolContext &ctx, const QRect &rect);

private:
    Ps::ToolId m_id;
};

} // namespace Ps

#endif // TOOL_H
