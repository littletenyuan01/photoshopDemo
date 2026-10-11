/**
 * extremumfilterop.h — 最大值 / 最小值（排序统计滤波，滑窗直方图）。
 *
 * 对照 PS「滤镜→其它→最大值/最小值」（膨胀 / 腐蚀），与 GEGL `gegl:value-propagate`
 * 的 `MODE_MORPHOLOGICAL_MAX` / `MIN` 同一类效果。
 *
 * 【与中值的关系】三者是同一族的**排序统计滤波**（窗口直方图里取一个分位）：
 *   · 中值 = 50% 分位（`MedianBlurFilterOp`，用增量 cum 定位）
 *   · 最大值 = 100% 分位（本类，取最高非空 bin）
 *   · 最小值 = 0% 分位（本类，取最低非空 bin）
 * 极值比中值简单得多：不需要 cum，只维护「当前极值 bin」，进出样本时按需上下扫描。
 * 两个算子合一个类（构造时带模式），`opsInit` 里分别注册。
 *
 * 【abyss】越界样本按透明（0）计入直方图，窗口样本数恒为 (2r+1)²（与 BoxBlur / 中值同口径）。
 * 【直通 alpha】先 `qUnpremultiply`，在直通通道上取极值，最后 `qPremultiply` 写回。
 */
#ifndef ENGINE_OP_EXTREMUMFILTEROP_H
#define ENGINE_OP_EXTREMUMFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 最大值 / 最小值（滑窗直方图；构造时定方向）。 */
class ExtremumFilterOp : public FilterOp
{
public:
    enum class Mode { Maximum, Minimum };

    explicit ExtremumFilterOp(Mode mode) : m_mode(mode) {}

    OpName name() const override
    {
        return m_mode == Mode::Maximum ? OpName::Maximum : OpName::Minimum;
    }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    Mode m_mode = Mode::Maximum;
    int m_radius = 0;
    /// 窗口直方图（4 通道 × 256 bin）；常驻小资源，与 ROI 尺寸无关。process 每行重建。
    quint16 m_hist[4][256] = {};
    /// 每通道当前极值 bin（跨像素维护，随滑窗增量更新）。
    quint8 m_ext[4] = {};
};

} // namespace Ps

#endif // ENGINE_OP_EXTREMUMFILTEROP_H
