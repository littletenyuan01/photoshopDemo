/**
 * offsetfilterop.h — 位移（**几何算子**：把像素整体搬 (dx,dy)）。
 *
 * 对照 PS「滤镜→其它→位移」与 GEGL `gimp:offset`
 * （参考实现 `gimp-master/app/operations/gimpoperationoffset.c`）。
 *
 * 【它不是面积算子】本算子 `inputPad() == 0`：输出像素读的是**别处**的输入，
 * 不是邻居。因此**不能**参与「分块 + 对称外扩」的逐块求值 —— 那要么按
 * `max(|dx|,|dy|)` 外扩（偏移 500px 时每块多取 276 倍），要么在块界错位。
 * 故它只在**整幅**栅格上求值：`Layer::compositeRasterRegion` 检测到栈里有几何算子时，
 * 退化为「整层求值一次，再按请求区裁」，与样式（需要外扩 halo）走整层缓存是同一类取舍。
 *
 * 【abyss / wrap 策略】两种，对齐 GEGL 的 `GeglAbyssPolicy` 里最常用的两个：
 *  · **wrap**（默认，PS 语义）= 环绕：移出画布的像素从另一侧回来（`GIMP_OFFSET_WRAP`）；
 *  · **transparent** = 移出即丢，空出的地方透明。
 * 未做：`CLIP` / `LOOP` / `BLACK` / `WHITE`（GEGL 还有这些，本项目按需再加）。
 *
 * 【与「移动工具」的区别】移动工具改的是图层 `offset`（不搬像素、可继续编辑）；
 * 本算子是**滤镜**（非破坏地改写像素映射），会进滤镜栈、可开关与撤销。
 */
#ifndef ENGINE_OP_OFFSETFILTEROP_H
#define ENGINE_OP_OFFSETFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 位移（移出即丢、空出透明；见 .cpp 顶部「为什么不做环绕」）。 */
class OffsetFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Offset; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_dx = 0;
    int m_dy = 0;
};

} // namespace Ps

#endif // ENGINE_OP_OFFSETFILTEROP_H
