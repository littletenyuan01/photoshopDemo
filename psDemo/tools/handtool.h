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
 * 也负责通用的「中键拖拽 / Alt+左键拖拽」平移手势——由 ToolManager 兜底分派。
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

    /// 供 ToolManager 复用：该事件是否属于「通用平移手势」
    static bool isPanGesture(const ToolEvent &event);

private:
    bool m_panning = false;
    QPointF m_lastWidgetPos;
};

} // namespace Ps

#endif // HANDTOOL_H
