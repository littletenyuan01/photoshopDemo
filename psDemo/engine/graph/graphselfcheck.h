/**
 * graphselfcheck.h — 图引擎自检（engine/graph 层）。
 *
 * 无外部依赖的最小结构自测：菱形 DAG、拓扑序、下游集合、环检测。
 * 启动期调用（main.cpp），失败仅告警不阻断。
 */
#ifndef ENGINE_GRAPH_GRAPHSELFCHECK_H
#define ENGINE_GRAPH_GRAPHSELFCHECK_H

namespace Ps {

/** @return true 表示 PsGraph 结构语义自检全部通过。 */
bool graphSelfCheck();

} // namespace Ps

#endif // ENGINE_GRAPH_GRAPHSELFCHECK_H
