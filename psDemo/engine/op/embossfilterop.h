/**
 * embossfilterop.h — 浮雕（面积算子；3×3 方向性核 + 中灰偏置）。
 *
 * 对照 PS「滤镜→风格化→浮雕效果」（GIMP 侧对应 `gegl:emboss`）。
 * 核（强度 s）与偏置：
 * ```
 *   -s  -s   0
 *   -s   0   s      ← 核**权和 = 0** ⇒ 平坦区域结果为 0
 *    0   s   s         再统一 + 128 ⇒ 平坦区域呈**中灰**（这才是浮雕的观感）
 * ```
 * 于是平坦处 = 128 灰、朝一个方向变亮/另一个方向变暗，得到「凸起」的立体感。
 *
 * 【与其它 3×3 算子的区别】锐化/查找边缘的核权和非 0（保持均值 / 取梯度幅度），
 * 浮雕故意权和为 0 并加偏置 —— 这是**观感需求**，不是笔误。
 *
 * 【alpha 不参与卷积】浮雕对 alpha 没有意义（会造出半透明的「浮雕边缘」），
 * 故 alpha 原样保留 —— 与「模糊类算子连 alpha 一起滤」不同，这是刻意的差异。
 *
 * 【资源】只缓存 2 行原始（直通）像素做滚动窗口（O(w)），与 `FindEdges` / `Sharpen` 同构。
 * 【abyss】越界样本按透明（0）算（与其它面积算子一致）。
 */
#ifndef ENGINE_OP_EMBOSSFILTEROP_H
#define ENGINE_OP_EMBOSSFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 浮雕（3×3 方向性核 + 128 中灰偏置，强度 1..4）。 */
class EmbossFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Emboss; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_strength = 1;
};

} // namespace Ps

#endif // ENGINE_OP_EMBOSSFILTEROP_H
