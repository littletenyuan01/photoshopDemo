/**
 * highpassfilterop.h — 高通保留（src - 盒模糊 + 128）。
 *
 * 对照 PS「滤镜→其它→高通保留」与 GEGL `gegl:high-pass`：
 * `out = src - blur(src, r) + 128` —— 平坦区呈中灰 128，边缘被保留下来。
 *
 * 【模糊级复用】直接复用 `BoxBlurFilterOp`（走它自己的三段式，与 USM 同套路）——
 * 这再次证明 `FilterOp` 族可组合：高通 = 源 + 负模糊 + 偏置。
 *
 * 【alpha】不参与高通、原样保留（对 alpha 做高通只会造出半透明边缘）。
 * 【abyss】模糊级是盒模糊（abyss=透明），高通偏置 128 后边缘仍偏中灰，属预期。
 */
#ifndef ENGINE_OP_HIGHPASSFILTEROP_H
#define ENGINE_OP_HIGHPASSFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 高通保留（src - 盒模糊 + 128）。 */
class HighPassFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::HighPass; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 0;
};

} // namespace Ps

#endif // ENGINE_OP_HIGHPASSFILTEROP_H
