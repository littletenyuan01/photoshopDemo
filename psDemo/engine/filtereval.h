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

void applyNode(QImage &image, const FilterNode &node);

} // namespace FilterEval
} // namespace Ps

#endif // ENGINE_FILTEREVAL_H
