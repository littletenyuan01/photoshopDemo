#ifndef PAINTBUCKETTOOL_H
#define PAINTBUCKETTOOL_H

#include "tool.h"

namespace Ps {

/**
 * 油漆桶工具（tools 层）。
 *
 * 【职责】只处理指针事件与取色；不写像素算法。
 * 【对照 GIMP】`app/tools/gimpbucketfilltool.c`：
 *   button_press → 校验可见/可编辑 → 组装 FillOptions → 调 core 填充。
 * 像素写入在 `engine/PaintEngine::floodFill`
 * （对应 `gimpdrawable-bucket-fill.c` + `gimppickable-contiguous-region.cc`）。
 *
 * 选项映射（见 ToolContext）：
 * - fillSource → GimpBucketFillMode（FG / BG；PATTERN 未做）
 * - fillTolerance → threshold（GIMP 默认 15，本 UI 默认 32 对齐 PS）
 * - fillContiguous → 连续开关（PS）；GIMP 相似色路径固定 by_seed
 * - fillOpacity → context opacity（此处烘焙进颜色 alpha，无 paint-mode）
 */
class PaintBucketTool : public Tool
{
    Q_OBJECT

public:
    explicit PaintBucketTool(QObject *parent = nullptr);

    QCursor cursor() const override;

    /** 左键在活动层种子点触发一次填充。 */
    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
};

} // namespace Ps

#endif // PAINTBUCKETTOOL_H
