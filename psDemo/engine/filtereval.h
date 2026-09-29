/**
 * filtereval.h — 滤镜节点求值（engine 层）。
 *
 * 对临时 QImage 就地处理，不写 Layer 瓦片；由 FilterStack::apply 在合成前调用。
 * 对照 GIMP drawable filter process。
 */
#ifndef ENGINE_FILTEREVAL_H
#define ENGINE_FILTEREVAL_H

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

} // namespace FilterEval
} // namespace Ps

#endif // ENGINE_FILTEREVAL_H
