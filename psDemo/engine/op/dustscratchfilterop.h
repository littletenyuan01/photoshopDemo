/**
 * dustscratchfilterop.h — 蒙尘与划痕 / 减少杂色（中值组合；批次 A）。
 *
 * Demo 语义（非 PS 全功能）：
 *  · DustAndScratches：先中值，仅当 |src−med| 任一通道 > 阈值 时用中值；
 *  · ReduceNoise：out = lerp(src, med, strength%)。
 *
 * 中值实现复用 MedianBlurFilterOp，不复制滑窗直方图。
 */
#ifndef ENGINE_OP_DUSTSCRATCHFILTEROP_H
#define ENGINE_OP_DUSTSCRATCHFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 蒙尘与划痕（中值 + 阈值替换）。 */
class DustAndScratchesFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::DustAndScratches; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 1;
    int m_threshold = 10;
};

/** 减少杂色（中值 + 强度混合）。 */
class ReduceNoiseFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::ReduceNoise; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 2;
    int m_strength = 50;
};

} // namespace Ps

#endif // ENGINE_OP_DUSTSCRATCHFILTEROP_H
