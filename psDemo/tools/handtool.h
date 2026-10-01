/**
 * handtool.h — 抓手工具声明（tools 层）。
 *
 * 拖拽平移画布；isPanGesture 供 CanvasView 判定中键临时平移。
 */
#ifndef HANDTOOL_H
#define HANDTOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

/**
 * 抓手工具：拖拽平移画布（tools 层）。
 *
 * 【对照 GIMP】app/tools/gimpmovetool.c 里的平移分支与 GimpDisplayShell 的滚动；
 * 本项目不依赖显示层类型，只通过 ViewPort::panBy 请求平移。
 *
 * 临时平移：空格（CanvasView 临时切本工具）与中键拖拽；不用 Alt+左键（留给取色/图章设源等）。
 */
class HandTool : public Tool
{
    Q_OBJECT

public:
    explicit HandTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

    /// 供 CanvasView：是否为中键临时平移手势（不含空格，空格走临时切工具）
    static bool isPanGesture(const ToolEvent &event);

private:
    bool m_panning = false;
    QPointF m_lastWidgetPos;
};

} // namespace Ps

#endif // HANDTOOL_H
