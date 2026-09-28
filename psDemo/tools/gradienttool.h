#ifndef GRADIENTTOOL_H
#define GRADIENTTOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

/**
 * 渐变工具（tools 层）。
 *
 * 【职责】只管拖拽起止与浮层预览线；写像素在 PaintEngine::applyGradient。
 * 【对照 GIMP】`app/tools/gimpgradienttool.c`（继承 DrawTool：
 *   press 记录 start → motion 更新 end → release/commit 调 drawable-gradient）。
 * 本项目瘦身为「拖完立刻提交」（≈ instant 模式），无 GEGL 实时预览滤镜、无端点编辑器。
 *
 * 选项映射见 ToolContext（gradient-type / offset / dither / gradient-reverse / opacity）。
 */
class GradientTool : public Tool
{
    Q_OBJECT

public:
    explicit GradientTool(QObject *parent = nullptr);

    QCursor cursor() const override;

    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    bool m_dragging = false;
    QPointF m_startImage;
    QPointF m_endImage;
    QPointF m_startWidget; ///< 浮层用控件坐标
    QPointF m_endWidget;
};

} // namespace Ps

#endif // GRADIENTTOOL_H
