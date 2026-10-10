/**
 * psgraph.cpp — psgraph.h 实现（engine/graph 层）。
 *
 * 建节点 / 连边 / 结构校验 / Kahn 拓扑排序（含环检测）/ 下游集合 / ROI 求值。
 */
#include "psgraph.h"

#include <QHash>
#include <QQueue>

namespace Ps {

GraphNode::Id PsGraph::addNode(std::shared_ptr<GraphOp> op)
{
    const GraphNode::Id id = m_nodes.size();
    GraphNode nd(id);
    nd.setOp(std::move(op));
    m_nodes.append(std::move(nd));
    return id;
}

GraphNode *PsGraph::node(GraphNode::Id id)
{
    return isValidId(id) ? &m_nodes[id] : nullptr;
}

const GraphNode *PsGraph::node(GraphNode::Id id) const
{
    return isValidId(id) ? &m_nodes[id] : nullptr;
}

bool PsGraph::connect(GraphNode::Id from, GraphNode::Id to, int toInput)
{
    if (!isValidId(from) || !isValidId(to) || toInput < 0)
        return false;
    m_nodes[to].setInput(toInput, from);
    return true;
}

QVector<GraphNode::Id> PsGraph::allNodeIds() const
{
    QVector<GraphNode::Id> ids;
    ids.reserve(m_nodes.size());
    for (int i = 0; i < m_nodes.size(); ++i)
        ids.append(i);
    return ids;
}

bool PsGraph::validate(QString *error) const
{
    const int n = m_nodes.size();
    for (int u = 0; u < n; ++u) {
        const QVector<GraphNode::Id> &ins = m_nodes[u].inputs();
        for (int i = 0; i < ins.size(); ++i) {
            const GraphNode::Id w = ins[i];
            if (w == GraphNode::kInvalidId) {
                if (error)
                    *error = QStringLiteral("PsGraph: 节点 %1 的输入口 %2 未连接")
                                 .arg(u).arg(i);
                return false;
            }
            if (!isValidId(w)) {
                if (error)
                    *error = QStringLiteral("PsGraph: 节点 %1 的输入口 %2 指向非法节点 %3")
                                 .arg(u).arg(i).arg(w);
                return false;
            }
        }
    }

    // 环检测并入拓扑排序：排序不完整即有环
    return topoSort(error).size() == n;
}

QVector<GraphNode::Id> PsGraph::topoSort(QString *error) const
{
    const int n = m_nodes.size();

    // 入度 = 该节点依赖的上游数（合法输入口个数）
    QVector<int> indegree(n, 0);
    // 邻接：上游 w → 下游列表（即「谁以 w 为输入」）
    QVector<QVector<GraphNode::Id>> adj(n);
    for (int u = 0; u < n; ++u) {
        for (GraphNode::Id w : m_nodes[u].inputs()) {
            if (w == GraphNode::kInvalidId || !isValidId(w))
                continue;
            ++indegree[u];
            adj[w].append(u);
        }
    }

    QQueue<GraphNode::Id> ready;
    for (int u = 0; u < n; ++u)
        if (indegree[u] == 0)
            ready.enqueue(u);

    QVector<GraphNode::Id> order;
    order.reserve(n);
    while (!ready.isEmpty()) {
        const GraphNode::Id u = ready.dequeue();
        order.append(u);
        for (GraphNode::Id v : adj[u]) {
            if (--indegree[v] == 0)
                ready.enqueue(v);
        }
    }

    if (order.size() != n) {
        if (error)
            *error = QStringLiteral("PsGraph: 图中存在环，无法拓扑排序");
        return {};
    }
    return order;
}

QVector<GraphNode::Id> PsGraph::downstreamOf(GraphNode::Id id) const
{
    QVector<GraphNode::Id> result;
    if (!isValidId(id))
        return result;

    const int n = m_nodes.size();
    QHash<GraphNode::Id, bool> visited;
    QQueue<GraphNode::Id> frontier;
    frontier.enqueue(id);
    visited.insert(id, true);

    while (!frontier.isEmpty()) {
        const GraphNode::Id u = frontier.dequeue();
        if (u != id)
            result.append(u);
        // 找所有以 u 为直接输入的下游
        for (int v = 0; v < n; ++v) {
            if (visited.contains(v))
                continue;
            if (m_nodes[v].inputs().contains(u)) {
                visited.insert(v, true);
                frontier.enqueue(v);
            }
        }
    }
    return result;
}

QImage PsGraph::evaluate(GraphNode::Id id, const QRect &region)
{
    GraphNode *nd = node(id);
    if (!nd || !nd->op())
        return {};

    // 缓存命中：同区域且未失效，直接复用（含上游已失效时本节点会被级联失效）
    if (nd->cacheValid() && nd->cacheRegion() == region)
        return nd->cacheImage();

    GraphOp *op = nd->op();
    const QVector<QRect> req = op->requiredInputs(region);
    if (req.size() != op->inputCount())
        return {}; // 算子声明不一致

    QVector<QImage> ins;
    ins.reserve(req.size());
    for (int i = 0; i < req.size(); ++i) {
        const GraphNode::Id up = nd->input(i);
        if (up == GraphNode::kInvalidId)
            return {};
        QImage img = evaluate(up, req[i]);
        if (img.isNull() && !req[i].isEmpty())
            return {}; // 上游求值失败
        ins.append(img);
    }

    QImage out = op->process(ins, region);
    nd->setCache(out, region);
    return out;
}

void PsGraph::invalidateFrom(GraphNode::Id id)
{
    if (GraphNode *nd = node(id))
        nd->invalidateCache();
    for (GraphNode::Id d : downstreamOf(id))
        if (GraphNode *nd = node(d))
            nd->invalidateCache();
}

void PsGraph::bumpVersion(GraphNode::Id id)
{
    if (GraphNode *nd = node(id))
        nd->bumpVersion();
    invalidateFrom(id);
}

} // namespace Ps
