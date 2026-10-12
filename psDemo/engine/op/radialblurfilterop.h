/**
 * radialblurfilterop.h — 径向模糊（旋转 / 缩放；中心为图心 → 整层路径）。
 */
#ifndef ENGINE_OP_RADIALBLURFILTEROP_H
#define ENGINE_OP_RADIALBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

class RadialBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::RadialBlur; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_amount = 10;
    int m_method = 0; ///< 0=旋转，1=缩放
};

} // namespace Ps

#endif
