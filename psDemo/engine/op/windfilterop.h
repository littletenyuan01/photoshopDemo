/**
 * windfilterop.h — 风（沿扫描线取上风向最大亮度；方向/方法/强度）。
 */
#ifndef ENGINE_OP_WINDFILTEROP_H
#define ENGINE_OP_WINDFILTEROP_H

#include "filterop.h"

namespace Ps {

class WindFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Wind; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_dir = 0;      ///< 0=从左吹向右，1=从右吹向左
    int m_method = 0;   ///< 0=风，1=爆发，2=交错
    int m_strength = 5;
};

} // namespace Ps

#endif
