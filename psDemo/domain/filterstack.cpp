/**
 * filterstack.cpp — FilterStack 节点管理与 FilterEval 串联求值（domain 层）。
 */
#include "filterstack.h"

#include "engine/filtereval.h"
#include "engine/graph/pointfilterop.h"
#include "engine/graph/psgraph.h"
#include "engine/graph/sourceop.h"

#include <memory>

namespace Ps {

/** 追加到栈顶，返回新下标。 */
int FilterStack::append(const FilterNode &node)
{
    m_nodes.append(node);
    return m_nodes.size() - 1;
}

bool FilterStack::removeAt(int index)
{
    if (index < 0 || index >= m_nodes.size())
        return false;
    m_nodes.removeAt(index);
    return true;
}

bool FilterStack::setEnabled(int index, bool on)
{
    if (index < 0 || index >= m_nodes.size())
        return false;
    m_nodes[index].setEnabled(on);
    return true;
}

bool FilterStack::hasEnabled() const
{
    for (const FilterNode &n : m_nodes) {
        if (n.isEnabled())
            return true;
    }
    return false;
}

/** 对 source 拷贝自底向顶应用已启用节点；无启用节点时原样返回。 */
QImage FilterStack::apply(const QImage &source) const
{
    if (source.isNull() || !hasEnabled())
        return source;

    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 建链图：source → f1 → f2 → …（对照 GimpFilterStack 的 GeglNode *graph）
    // 点滤镜 ROI 恒等，逐像素结果与旧循环一致；图求值在此即「线性链的特例」。
    PsGraph graph;
    GraphNode::Id prev = graph.addNode(std::make_shared<ImageSourceOp>(out));
    for (const FilterNode &node : m_nodes) {
        if (!node.isEnabled())
            continue;
        const GraphNode::Id cur = graph.addNode(std::make_shared<PointFilterOp>(node));
        graph.connect(prev, cur, 0);
        prev = cur;
    }
    return graph.evaluate(prev, out.rect());
}

} // namespace Ps
