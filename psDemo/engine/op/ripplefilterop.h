/**
 * ripplefilterop.h — 波纹（几何重映射代表项）。
 *
 * Demo 语义：`out(x,y) = in(x + A·sin(2π·y/λ), y)`（水平正弦位移）。
 * 输入需求 = 输出区左右各扩 A —— 单矩形，可走逐块路径（验证任意重映射进滤镜链）。
 */
#ifndef ENGINE_OP_RIPPLEFILTEROP_H
#define ENGINE_OP_RIPPLEFILTEROP_H

#include "filterop.h"

namespace Ps {

class RippleFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Ripple; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_amplitude = 5;
    int m_wavelength = 16;
};

} // namespace Ps

#endif // ENGINE_OP_RIPPLEFILTEROP_H
