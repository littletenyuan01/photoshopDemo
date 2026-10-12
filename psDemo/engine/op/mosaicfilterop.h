/**
 * mosaicfilterop.h — 马赛克（方块均值；格子对齐依赖绝对位置 → 整层路径）。
 */
#ifndef ENGINE_OP_MOSAICFILTEROP_H
#define ENGINE_OP_MOSAICFILTEROP_H

#include "filterop.h"

namespace Ps {

class MosaicFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Mosaic; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_cell = 8;
};

} // namespace Ps

#endif
