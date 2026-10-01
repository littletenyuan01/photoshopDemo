/**
 * magicwandtool.h — 魔棒选区工具（tools 层）。
 *
 * 对照 GIMP Fuzzy Select（gimpfuzzyselecttool.c → contiguous_region_by_seed/color）。
 * 单击种子点，经 selectFlood → SelectFloodOp 写入文档选区。
 */
#ifndef MAGICWANDTOOL_H
#define MAGICWANDTOOL_H

#include "domain/selection.h"
#include "tool.h"

namespace Ps {

class MagicWandTool : public Tool
{
    Q_OBJECT

public:
    explicit MagicWandTool(QObject *parent = nullptr);

    QCursor cursor() const override;
    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
};

} // namespace Ps

#endif // MAGICWANDTOOL_H
