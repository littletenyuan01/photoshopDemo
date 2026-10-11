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
 *
 * 【找中值：增量 cum（不再每像素扫 256 bin）】
 * 直方图定位中值原本是「从 bin 0 累加到 ≥ 第 half 小」，即每像素每通道最多 256 次访存 ——
 * 实测这一项占了总开销的大头（滑窗更新每像素只有 4×(2r+1)≈28 次）。
 * 现在改为**维护中值位置**：滑一格只会让窗口少 2r+1 个样本、多 2r+1 个样本，
 * 因此「比当前中值 bin 小的样本数」（cum）每次只需按进出样本修正，
 * 中值 bin 也只需上下移动 **≤ 2r+1 个 bin** 就能恢复
 * `cum < half ≤ cum + hist[p]` 这个不变量 —— 单次定位成本 O(r)，与 256 无关。
 * 语义仍由 `areafilter` 的**朴素排序中值 oracle**（r=1..4）逐像素钉住（方法互相独立）。
 *
 * 【为什么这段值得写这么多注释】把「每像素重建答案」换成「跨像素维护状态」后，旧实现里
 * **无害的写法全部变成致命**：越界样本要照常计数（口径守恒）、行号必须只有一条取槽路径
 * （未夹紧的 `-2 % 3 == -2` 会读到另一行）、回退循环必须有 bin 0 下界。三个都真的踩到了，
 * 详见 docs/pending-dev.md 的缺陷台账 #8/#9/#10。
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

    /**
     * 用**直接扫描**（从 bin 0 累加到 ≥ 第 half 小）定位通道 @p channel 的中值。
     *
     * 生产路径用的是增量 cum（见 .cpp），这条「朴素扫描」是给测试做**交叉验证**用的：
     * 两者建立在**同一张**刚由滑窗维护出来的直方图上，所以能精确回答
     * 「错的是直方图内容，还是中值定位」——本轮调试正是靠它把范围一步步缩小到
     * 「bin 0 下界」与「环形槽号」两处。
     */
    int locateMedianByScan(int channel) const;

private:
    int m_radius = 0;
    /// 窗口直方图（4 通道 × 256 bin）；常驻小资源，与 ROI 尺寸无关。process 每行重建。
    quint16 m_hist[4][256] = {};
    /// 增量 cum 定位中值的跨像素状态（按通道）：比 m_pos[c] bin 小的样本数、当前中值 bin。
    /// 这两个量随窗口平移增量维护，故**必须**在整行的 x 循环之间保持（每行开头重建直方图时复位）。
    int m_posCum[4] = {};
    int m_pos[4] = {};
};

} // namespace Ps

#endif // ENGINE_OP_MEDIANBLURFILTEROP_H
