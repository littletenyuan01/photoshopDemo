/**
 * fragmentfilterop.h — 碎片（四角偏移取样平均）。
 */
#ifndef ENGINE_OP_FRAGMENTFILTEROP_H
#define ENGINE_OP_FRAGMENTFILTEROP_H

#include "filterop.h"

namespace Ps {

class FragmentFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Fragment; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;
};

} // namespace Ps

#endif
