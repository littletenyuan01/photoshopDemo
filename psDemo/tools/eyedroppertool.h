/**
 * eyedroppertool.h — 吸管工具（tools 层）。
 *
 * 对照 GIMP Color Picker（gimpcolorpickertool.c）：单击采样合成色写入前景。
 * Alt+单击采背景（对齐 PS）；经 Tool::foregroundPicked / backgroundPicked 上报。
 */
#ifndef EYEDROPPERTOOL_H
#define EYEDROPPERTOOL_H

#include "tool.h"

namespace Ps {

class EyedropperTool : public Tool
{
    Q_OBJECT

public:
    explicit EyedropperTool(QObject *parent = nullptr);

    QCursor cursor() const override;
    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
};

} // namespace Ps

#endif // EYEDROPPERTOOL_H
