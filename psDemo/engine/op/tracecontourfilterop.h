/**
 * tracecontourfilterop.h — 等高线（亮度跨过阈值的 4-邻接画黑线）。
 */
#ifndef ENGINE_OP_TRACECONTOURFILTEROP_H
#define ENGINE_OP_TRACECONTOURFILTEROP_H

#include "filterop.h"

namespace Ps {

class TraceContourFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::TraceContour; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_level = 128;
    int m_edge = 0; ///< 0=较低，1=较高
};

} // namespace Ps

#endif
