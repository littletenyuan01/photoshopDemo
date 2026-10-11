/**
 * medianblurfilterop.h — 中值模糊（面积算子；滑窗直方图）。
 *
 * 对照 `gegl:median-blur`（GIMP 滤镜→杂色→中间值 / PS「滤镜→杂色→中间值」）。
 * 本实现是它的**子集**：只做方形邻域 + 50% 分位（真中值），不做 percentile / circle / diamond。
 *
 * 【为什么用滑窗直方图，而不是「取窗口 → 排序 → 取中」】
 * 朴素做法每像素要排序 `(2r+1)²` 个样本（r=3 是 49 个，排序 ~300 次比较 ×4 通道）；
 * 滑窗直方图把「窗口平移一格」的代价压到 **2(2r+1) 次计数增减**（r=3 是 14 次/通道），
 * 与窗口面积无关 —— 这正是中值能从「几十毫秒」变成「可用」的关键。
 * 代价：找中值要扫 256 个 bin（每通道），当前实现是**直接扫**；「用增量 cum 维护中值位置」
 * 是已知的下一步优化（见 docs/pending-dev.md，未做，不宣称）。
 *
 * 【资源】本算子唯一的常驻资源是 4×256 的窗口直方图（恒 2 KB，**与 ROI 尺寸无关**）——
 * 所以不需要 `BoxBlurFilterOp` 那套「超过上限就放掉」的逻辑。这是刻意保留的**对照**：
 * 「资源」未必是区域尺寸的，也可能是 O(1) 的小状态。
 *
 * 【abyss】越界样本按**透明（0）**计入直方图（与 BoxBlur 同口径：窗口样本数恒为 (2r+1)²，
 * 不做重归一化）。
 *
 * 【直通 alpha】与 BoxBlur 同口径：先整体 `qUnpremultiply`，在直通通道上取中值，
 * 最后 `qPremultiply` 写回（alpha 通道同样参与中值）。
 */
#ifndef ENGINE_OP_MEDIANBLURFILTEROP_H
#define ENGINE_OP_MEDIANBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 中值模糊（滑窗直方图；方形邻域 + 50% 分位）。 */
class MedianBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::MedianBlur; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_radius = 0;
    /// 窗口直方图（4 通道 × 256 bin）；常驻小资源，与 ROI 尺寸无关。process 每行重建。
    quint16 m_hist[4][256] = {};
};

} // namespace Ps

#endif // ENGINE_OP_MEDIANBLURFILTEROP_H
