/**
 * findedgesfilterop.h — 查找边缘（面积算子；3×3 Sobel）。
 *
 * 对照 `gegl:edge-sobel`（GIMP 滤镜→边缘检测→Sobel）/ PS「滤镜→风格化→查找边缘」。
 *
 * 卷积核（标准 Sobel）：
 * ```
 *   gx = [-1 0 +1; -2 0 +2; -1 0 +1]
 *   gy = [-1 -2 -1;  0 0  0; +1 +2 +1]
 * ```
 * 幅度 = √(gx²+gy²)，**逐通道**计算后夹到 0..255；alpha 原样保留。
 *
 * 【直通 alpha】与 `BoxBlur` 同口径：先 `qUnpremultiply` 整图，在**直通**通道上求导，最后
 * `qPremultiply` 写回。若直接在预乘像素上求导再写回预乘缓冲，等于**二次预乘** ——
 * 半透明边缘会变得又暗又钝（此坑在写语义 oracle 时自捕获，见 docs/pending-dev.md 缺陷台账 #6）。
 *
 * 【abyss 取「透明」】与 `BoxBlur` 同口径（层外即透明）。GIMP 的 edge-sobel 用 clamp ——
 * 这里刻意与项目既有约定保持一致，差异记在 docs/pending-dev.md。
 *
 * 【资源】3×3 只需要「上两行**原始**像素」的滚动窗口（2×width 个 QRgb），**不需要整图临时缓冲**：
 * 逐行写回会破坏下一次要用的邻域，所以把 y-1 / y 两行原始像素留在滚动缓冲里，第 y+1 行直接读
 * （此刻它还没被写过）。这样比「整图拷一份再卷积」少一整趟访存。
 */
#ifndef ENGINE_OP_FINDEDGESFILTEROP_H
#define ENGINE_OP_FINDEDGESFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 查找边缘（3×3 Sobel；无参数）。 */
class FindEdgesFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::FindEdges; }

    void process(QImage &image, const FilterContext &ctx) override;
};

} // namespace Ps

#endif // ENGINE_OP_FINDEDGESFILTEROP_H
