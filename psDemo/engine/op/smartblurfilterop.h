/**
 * smartblurfilterop.h — 特殊模糊（保边 + 模式：正常 / 仅边缘 / 叠加边缘）。
 */
#ifndef ENGINE_OP_SMARTBLURFILTEROP_H
#define ENGINE_OP_SMARTBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

class SmartBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::SmartBlur; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 3;
    int m_threshold = 25;
    int m_mode = 0; ///< 0 正常，1 仅边缘，2 叠加边缘
};

} // namespace Ps

#endif
