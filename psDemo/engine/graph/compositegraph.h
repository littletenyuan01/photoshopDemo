/**
 * compositegraph.h — 合成图构建器（engine/graph 层）。
 *
 * 把 ImageDocument 的图层栈编成一张 DAG：
 * 透明源 →（层源 → 滤镜链 → 混合/调整应用）… → sink。
 * 对照 GIMP gimp-gegl-nodes / gimpprojection 的图构建。
 */
#ifndef ENGINE_GRAPH_COMPOSITEGRAPH_H
#define ENGINE_GRAPH_COMPOSITEGRAPH_H

#include "psgraph.h"

namespace Ps {

class ImageDocument;

/**
 * 合成图构建（对照 GIMP 投影把图层/混合/蒙版/滤镜拼成节点图）。
 *
 * 结构与 Compositor::blendLayerRange 一一对应，但用节点图表达，配合
 * PsGraph 的 ROI / 缓存 / 脏传播（Stage 3）。逐像素走同一份 compositepixel 代码，
 * 保证与 Compositor 结果一致。
 */
namespace CompositeGraph {

/**
 * 构建合成图；@p graph 由调用方持有（可跨次复用 + 配合 bumpVersion/invalidateFrom）。
 * @return sink 节点 id。
 */
GraphNode::Id build(PsGraph &graph, const ImageDocument &doc);

} // namespace CompositeGraph

} // namespace Ps

#endif // ENGINE_GRAPH_COMPOSITEGRAPH_H
