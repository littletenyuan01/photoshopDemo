/**
 * sharpenfilterop.h — 锐化（面积算子；3×3 线性卷积）。
 *
 * 对照 PS「滤镜→锐化→锐化 / 进一步锐化」与 GIMP 的 `gimp:sharpen`（3×3 线性核）。
 * 核（强度 s，默认 1）：
 * ```
 *    0   -s    0
 *   -s  1+4s  -s       ← 核权重和 = 1（保持均值，只增强高频）
 *    0   -s    0
 * ```
 * s=1 即经典锐化（中心 5、四邻 -1）；s 越大越接近「进一步锐化」。
 *
 * 【与 USM 锐化的分工】USM 用「模糊级 + 阈值」做可控锐化（半径可调、可抑制噪点）；
 * 本算子是**固定 3×3 邻域**的高频增强，快得多、但没有半径与阈值控制 —— 两者在 PS 里也并存。
 *
 * 【资源】只缓存 2 行原始（直通）像素做滚动窗口（O(w)），不做整图拷贝 —— 与 `FindEdges` 同构。
 *
 * 【abyss】越界样本按透明（0）算（与其它面积算子一致）；因为核权和为 1，边界不会整体漂移，
 * 只会在层边缘出现一圈「未锐化」的过渡。
 *
 * 【直通 alpha】同样先 `qUnpremultiply`、在直通通道上卷积、最后 `qPremultiply`（alpha 也参与）。
 */
#ifndef ENGINE_OP_SHARPENFILTEROP_H
#define ENGINE_OP_SHARPENFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 锐化（3×3 线性核，强度 1..4）。 */
class SharpenFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Sharpen; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_strength = 1;
};

} // namespace Ps

#endif // ENGINE_OP_SHARPENFILTEROP_H
