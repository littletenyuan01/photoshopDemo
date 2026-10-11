/**
 * gaussianblurfilterop.h — 高斯模糊（面积算子；用**三次盒模糊**逼近，GIMP / 经典的近似做法）。
 *
 * 【为什么用三次盒模糊，而不是直接卷积高斯核】
 *  · 可分离高斯核在 σ 大时核宽 = 6σ+1，逐像素成本随 σ **线性增长**；三次盒模糊每趟是滑动窗口
 *    求和（O(1)/像素、与 σ 无关），且能**直接复用已优化过的 `BoxBlurFilterOp`**（走它自己的三段式）。
 *  · 代价是它**只是逼近**、不是精确高斯。误差不靠「差不多」描述，而是用测试里的**朴素精确高斯**
 *    做量化对照（见 docs/pending-dev.md 的数据表）。
 *
 * 【σ 口径】本 Demo 把 UI 的「半径」映射为 **σ = 半径 / 3**（即「可见影响范围 ≈ 半径」）。
 * PS 未公开其「半径↔σ」的精确映射，所以这里把口径写死，并用**量化误差**代替「和 PS 一样」这种含糊说法。
 *
 * 【盒宽公式】经典三趟逼近：
 * ```
 *   wIdeal = sqrt(12σ²/n + 1),  n = 3
 *   wl = 不大于 wIdeal 的最大奇数;  wu = wl + 2
 *   前 m 趟用 wl、其余用 wu；m 由二次拟合给出（见 .cpp）
 * ```
 * 每趟盒宽 s 对应半径 `(s-1)/2`，交给盒模糊算子执行。
 *
 * 【资源】本算子**自己不持有缓冲**（三趟的临时缓冲由盒模糊算子自己的实例管理）——
 * 正好说明 `FilterOp` 的 `prepare/finish` 对「无资源算子」是可选实现。
 */
#ifndef ENGINE_OP_GAUSSIANBLURFILTEROP_H
#define ENGINE_OP_GAUSSIANBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 高斯模糊（三次盒模糊逼近；对照 gegl:gaussian-blur）。 */
class GaussianBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::GaussianBlur; }

    void process(QImage &image, const FilterContext &ctx) override;

    /**
     * 由「高斯半径」算出三次盒模糊各自的**盒半径**（(盒宽-1)/2）。
     * @param radius UI 口径的高斯半径（≥1）
     * @param radii  输出 3 个盒半径
     */
    static void boxRadiiFor(int radius, int radii[3]);
};

} // namespace Ps

#endif // ENGINE_OP_GAUSSIANBLURFILTEROP_H
