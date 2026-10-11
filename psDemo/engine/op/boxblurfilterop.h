/**
 * boxblurfilterop.h — 盒模糊滤镜算子（面积算子；**首个**按 FilterOp 三段式落地的滤镜）。
 *
 * 【为什么它第一个走 FilterOp】盒模糊需要两块整块临时缓冲（直通 alpha 缓冲 + 水平趟输出），
 * 是典型的「有资源」滤镜：纳入三段式后，缓冲在 `prepare` 里按 ROI 尺寸申请、跨调用复用，
 * 不再每次 new（实测分配曾占 64×64 块调用的约 30%）。
 * 点滤镜没有临时资源，仍走 `FilterEval` 的直接路径 —— 三段式对它们是纯开销
 * （与 `PointOp`「不走 OpRunner 三段式」同一取舍）。
 */
#ifndef ENGINE_OP_BOXBLURFILTEROP_H
#define ENGINE_OP_BOXBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 可分离盒模糊（对照 gegl:box-blur 的两趟滑动窗口）。 */
class BoxBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::BoxBlur; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

    /** 超过此像素数就不再跨调用保留缓冲（大图在多线程下会白占内存）。 */
    static constexpr qint64 kMaxRetainedPixels = 1 << 20; // ~4 MB / 块

private:
    QImage m_straight; ///< 直通 alpha 缓冲（跨调用复用）
    QImage m_tmp;      ///< 水平趟输出（跨调用复用）
    QSize m_cached;    ///< m_straight / m_tmp 对应尺寸
    int m_radius = 0;
    bool m_oversized = false; ///< 本次尺寸超上限 → finish 放掉缓冲
};

} // namespace Ps

#endif // ENGINE_OP_BOXBLURFILTEROP_H
