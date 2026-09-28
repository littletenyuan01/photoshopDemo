#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

/**
 * 移动工具（tools 层）。
 *
 * 【功能】
 * 1. 按下时按像素点选最上层非透明层并设为活动层（图层面板经 activeLayerChanged 同步）；
 * 2. 拖拽平移该层的文档偏移（改 Layer offset，不搬瓦片像素）。
 *
 * 【对照 GIMP】
 * - `gimpmovetool.c` + `gimp_image_pick_layer`（!move_current 时点选）
 * - `gimp_item_translate` / `gimp_layer_real_translate`
 *
 * 本项目瘦身：无「仅移动当前层」开关（等价始终可点选）、无选区/路径移动。
 */
class MoveTool : public Tool
{
    Q_OBJECT

public:
    explicit MoveTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    bool m_dragging = false;
    QPointF m_lastImagePos; ///< 上一帧图像坐标，用于算整数 dx/dy
};

} // namespace Ps

#endif // MOVETOOL_H
