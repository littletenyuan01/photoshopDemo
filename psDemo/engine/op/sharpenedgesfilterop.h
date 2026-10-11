/**
 * sharpenedgesfilterop.h — 锐化边缘（Sobel 幅值作 mask × 锐化；批次 A）。
 *
 * out = lerp(src, sharpened, edgeMag/255)；边缘强处多锐化，平坦区接近原图。
 */
#ifndef ENGINE_OP_SHARPENEDGESFILTEROP_H
#define ENGINE_OP_SHARPENEDGESFILTEROP_H

#include "filterop.h"

namespace Ps {

class SharpenEdgesFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::SharpenEdges; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_strength = 1;
};

} // namespace Ps

#endif // ENGINE_OP_SHARPENEDGESFILTEROP_H
