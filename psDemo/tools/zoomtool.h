#ifndef ZOOMTOOL_H
#define ZOOMTOOL_H

#include "tool.h"

namespace Ps {

/**
 * 缩放工具：左键以点击处为锚点放大，右键缩小（tools 层）。
 *
 * 【对照 GIMP】app/tools/gimpzoomtool.c：GIMP 还带「点击处为锚点」与拖框缩放、
 * Ctrl 切换缩放方向等；本项目只保留左右键锚点缩放。
 *
 * 复用 ViewPort::zoomAt 而不是自己算 offset，使缩放数学只存在于显示层一处。
 */
class ZoomTool : public Tool
{
    Q_OBJECT

public:
    explicit ZoomTool(QObject *parent = nullptr);

    QString displayName() const override;
    QString hint() const override;
    Qt::CursorShape cursorShape() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;

private:
    /** 单次点击的缩放倍率 */
    static constexpr qreal kStep = 1.25;
};

} // namespace Ps

#endif // ZOOMTOOL_H
