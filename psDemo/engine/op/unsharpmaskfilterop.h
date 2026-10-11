/**
 * unsharpmaskfilterop.h — USM 锐化（面积算子；**复用盒模糊算子**作为模糊级）。
 *
 * 公式（对照 `gegl:unsharp-mask` / PS「滤镜→锐化→USM 锐化」）：
 * ```
 *   diff = src - blur(src, radius)
 *   |diff| > threshold ? out = src + amount * diff : out = src
 * ```
 * 阈值与 diff 都在 0..255 尺度上**逐通道**比较；alpha 原样保留（锐化 alpha 通常不是本意）。
 *
 * 【为什么复用 `BoxBlurFilterOp` 而不是自己写模糊】
 * 少一份可分离卷积实现（少一处「改了一处忘了另一处」的漂移风险），也顺带验证
 * 「`FilterOp` 族可组合」：本算子通过 `filterOpInstance(OpName::BoxBlur)` 走**对方自己的**
 * `prepare/process/finish`（纯虚接口，不需要知道对方类型）。
 *
 * 【为什么模糊结果留成「预乘」再逐像素反预乘】
 * 盒模糊算子内部会做直通↔预乘转换；若这边先转直通再喂给它，就会多一次**不必要的往返**
 * （每次往返在低 alpha 下有 ±1 LSB 舍入损失，且多一趟全图读改写）。故模糊缓冲保持预乘，
 * 只在合并那一步逐像素 `qUnpremultiply`。
 */
#ifndef ENGINE_OP_UNSHARPMASKFILTEROP_H
#define ENGINE_OP_UNSHARPMASKFILTEROP_H

#include "filterop.h"

namespace Ps {

/** USM 锐化（对照 gegl:unsharp-mask）。 */
class UnsharpMaskFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::UnsharpMask; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

    /** 与盒模糊同口径：只在小区域上跨调用保留缓冲（理由见 boxblurfilterop.h）。 */
    static constexpr qint64 kMaxRetainedPixels = 128 * 128;

private:
    QImage m_blurred; ///< 模糊级结果（预乘；跨调用复用）
    QSize m_cached;   ///< m_blurred 对应尺寸
    int m_radius = 0;
    qreal m_amount = 1.0;
    int m_threshold = 0;
    bool m_oversized = false;
};

} // namespace Ps

#endif // ENGINE_OP_UNSHARPMASKFILTEROP_H
