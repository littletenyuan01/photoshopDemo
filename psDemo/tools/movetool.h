#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "tool.h"

namespace Ps {

/**
 * 移动工具占位（tools 层）。
 *
 * 真正实现需要「图层位移 + 撤销」，属 Roadmap Phase 6 之后。
 * 现在存在的意义是给 ToolManager 提供**中性的兜底工具**：
 * 未接入逻辑的工具切过去时不消费任何事件，行为等同「什么都不做」，
 * 而不是意外继承上一个工具的行为。
 *
 * 【对照 GIMP】GimpMoveTool（app/tools/gimpmovetool.c）承担图层/选区位移；
 * 本项目暂只保留壳。
 */
class MoveTool : public Tool
{
    Q_OBJECT

public:
    explicit MoveTool(QObject *parent = nullptr);

    QString displayName() const override;
    QString hint() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
};

} // namespace Ps

#endif // MOVETOOL_H
