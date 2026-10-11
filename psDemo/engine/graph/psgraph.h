/**
 * psgraph.h — 有向无环图（engine/graph 层）。
 *
 * 节点集 + 有向边 + 结构校验 + 拓扑排序 + ROI 求值。
 * 对照 GEGL 节点图 / GimpFilterStack 的 `GeglNode *graph`。
 */
#ifndef ENGINE_GRAPH_PSGRAPH_H
#define ENGINE_GRAPH_PSGRAPH_H

#include "graphnode.h"

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <memory>

namespace Ps {

/**
 * 有向无环图（对照 GEGL 节点图）。
 *
 * 【职责】结构 + 求值调度：建节点、连边、校验、拓扑排序、下游集合、ROI 递归求值。
 * 【不负责】具体像素算法（由 GraphOp 实现）；缓存与脏传播见 Stage 3。
 *
 * 【存储】节点以 id = 下标 存于 QVector；构建期建好，求值期只读结构。
 */
class PsGraph
{
public:
    /** 新增节点并挂算子。@return 新节点 id（= 0..N-1 递增）。 */
    GraphNode::Id addNode(std::shared_ptr<GraphOp> op = nullptr);

    /** 按 id 取节点；越界返回 nullptr。 */
    GraphNode *node(GraphNode::Id id);
    const GraphNode *node(GraphNode::Id id) const;

    /** 把 @p from 的输出连到 @p to 的第 @p toInput 个输入口。 */
    bool connect(GraphNode::Id from, GraphNode::Id to, int toInput);

    int nodeCount() const { return m_nodes.size(); }

    /** 全部节点 id（0..N-1）。 */
    QVector<GraphNode::Id> allNodeIds() const;

    /**
     * 结构校验：所有输入口指向合法节点（kInvalidId = 未连接 → 报错）、无环。
     * 失败返回 false 并写 @p error（可空）。
     */
    bool validate(QString *error = nullptr) const;

    /**
     * 拓扑排序（Kahn 算法）。数据流方向下，上游总在下游之前。
     * 有环时返回空序列并写 @p error。
     */
    QVector<GraphNode::Id> topoSort(QString *error = nullptr) const;

    /**
     * 从 @p id 出发的全部下游节点集合（直接 + 间接；不含自身）。
     * 供 Stage 3 失效传播使用。
     */
    QVector<GraphNode::Id> downstreamOf(GraphNode::Id id) const;

    /**
     * 有多少个**节点输入口**消费 @p id 的输出。
     * ==1 表示只有唯一消费者，可把该缓冲就地交给它（免一次整图拷贝）。
     */
    int consumerCount(GraphNode::Id id) const;

    /**
     * 求值：产出节点 @p id 覆盖 @p region 的输出图。
     * 递归向上游索取 requiredInputs 区域；节点无算子或输入未连接返回空图。
     * 命中节点缓存（同区域且未失效）直接返回，否则重算并写缓存。
     */
    QImage evaluate(GraphNode::Id id, const QRect &region);

    /**
     * 失效传播：置 @p id 及其全部下游缓存失效（不改版本）。
     * 上游节点被重算后调用，确保下游不复用旧缓存。
     */
    void invalidateFrom(GraphNode::Id id);

    /**
     * 参数变更：@p id 版本 +1 并失效其与全部下游缓存。
     * 配合 GraphNode::setOp 替换算子使用（改参数后调用）。
     */
    void bumpVersion(GraphNode::Id id);

private:
    bool isValidId(GraphNode::Id id) const
    {
        return id >= 0 && id < m_nodes.size();
    }

    QVector<GraphNode> m_nodes; ///< 节点表；id = 下标
};

} // namespace Ps

#endif // ENGINE_GRAPH_PSGRAPH_H
