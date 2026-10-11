/**
 * filtereval.h — 滤镜节点求值（engine 层）。
 *
 * 对临时 QImage 就地处理，不写 Layer 瓦片；由 FilterStack::apply 在合成前调用。
 * 对照 GIMP drawable filter process。
 */
#ifndef ENGINE_FILTEREVAL_H
#define ENGINE_FILTEREVAL_H

#include <QRect>
#include <functional>

class QImage;

namespace Ps {

class FilterNode;

/**
 * 滤镜节点求值（对照 drawable filter 对 buffer 的 process）。
 * 只改传入的临时图，不碰 Layer 瓦片。
 */
namespace FilterEval {

/** 按 node.op() 就地修改 @p image（预乘 ARGB32）。 */
void applyNode(QImage &image, const FilterNode &node);

/**
 * 同上，但带**协作式取消**：返回 true 表示中止。
 *
 * 传空（= 不取消）与上面那条重载完全等价。面积算子在内部行循环里周期性查询它，
 * 用于「用户改了参数 / 撤销 / 关文档」时放弃一份已经没人要的结果
 * （见 engine/op/filterop.h 的 FilterContext::shouldAbort）。
 */
void applyNode(QImage &image, const FilterNode &node, const std::function<bool()> &shouldAbort);

/**
 * 同上，但**带几何**：@p srcRect / @p outRect 是「本次要产出的输出区」与「输入覆盖区」，
 * 两者都在**输入图自己的坐标系**里（ROI 通用化第 ① 步，见 docs/engine/roi.md §5）。
 *
 * 传空矩形 = 退化为 `image.rect()`（即「本地坐标」语义，与上面两条重载一致）。
 * 只有**调用方确实按外扩取过输入**（例如 `PointFilterOp` 走 `requiredInputs`）时才有必要
 * 显式传：那时 `srcRect` 比 `outRect` 大一圈 pad，算子据此知道自己手里多出来的边在哪。
 */
void applyNode(QImage &image, const FilterNode &node, const QRect &srcRect, const QRect &outRect,
               const std::function<bool()> &shouldAbort);

} // namespace FilterEval
} // namespace Ps

#endif // ENGINE_FILTEREVAL_H
