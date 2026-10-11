/**
 * customfilterop.h — 自定滤镜（3×3 卷积核 + 缩放/位移）。
 *
 * 对照 PS「滤镜→其它→自定…」：一个 3×3 权重网格，结果是
 * `out = clamp( (Σ w[i]·in[i]) / scale + offset, 0, 255 )`。
 * 权重 -999..999、缩放 0..9999（0 = 不除）、位移 -255..255。
 *
 * 【一个算子顶多个】改权重即可得到锐化、模糊、浮雕、边缘提取等一票 3×3 效果，
 * 演示时最能体现「算子框架可扩展」——加它，等于验证了「算子参数带数组」这条路径。
 *
 * 【alpha】不参与卷积、原样保留（与浮雕同口径）：对 alpha 做卷积只会造出半透明
 * 「边缘」，没有意义。
 *
 * 【abyss】越界样本按透明（0）计（与其它邻域算子一致）。
 * 【直通 alpha】先 `qUnpremultiply` 在直通通道卷积，最后 `qPremultiply` 写回。
 */
#ifndef ENGINE_OP_CUSTOMFILTEROP_H
#define ENGINE_OP_CUSTOMFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 自定滤镜（3×3 卷积核；权重 + 缩放 + 位移）。 */
class CustomFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::CustomFilter; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_w[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0}; ///< 行主序 3×3，默认恒等
    int m_scale = 1;
    int m_offset = 0;
};

} // namespace Ps

#endif // ENGINE_OP_CUSTOMFILTEROP_H
