/**
 * surfaceblurfilterop.h — 表面模糊（保边邻域均值；半径 + 阈值）。
 */
#ifndef ENGINE_OP_SURFACEBLURFILTEROP_H
#define ENGINE_OP_SURFACEBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

class SurfaceBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::SurfaceBlur; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 5;
    int m_threshold = 15;
};

} // namespace Ps

#endif
