/**
 * graphselfcheck.cpp — graphselfcheck.h 实现（engine/graph 层）。
 *
 * 验证图引擎：Stage 1 结构（菱形 DAG、拓扑、下游、环检测）
 * + Stage 3a 缓存与失效传播。
 */
#include "graphselfcheck.h"

#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "pointfilterop.h"
#include "psgraph.h"
#include "sourceop.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <memory>

namespace Ps {

namespace {

int indexOf(const QVector<GraphNode::Id> &order, GraphNode::Id id)
{
    for (int i = 0; i < order.size(); ++i)
        if (order[i] == id)
            return i;
    return -1;
}

/** Stage 1：结构语义（菱形 DAG、拓扑序、下游、环检测）。 */
bool checkStructure()
{
    // 菱形 DAG：A→B、A→C、B→D、C→D（D 有两个输入口：0←B、1←C）
    PsGraph g;
    const GraphNode::Id a = g.addNode();
    const GraphNode::Id b = g.addNode();
    const GraphNode::Id c = g.addNode();
    const GraphNode::Id d = g.addNode();
    g.connect(a, b, 0);
    g.connect(a, c, 0);
    g.connect(b, d, 0);
    g.connect(c, d, 1);

    QString err;
    if (g.nodeCount() != 4)
        return false;
    if (!g.validate(&err))
        return false;

    // 拓扑序：A 在 B/C 之前，B/C 在 D 之前
    const QVector<GraphNode::Id> order = g.topoSort(&err);
    if (order.size() != 4)
        return false;
    const int ia = indexOf(order, a);
    const int ib = indexOf(order, b);
    const int ic = indexOf(order, c);
    const int id = indexOf(order, d);
    if (ia < 0 || ib < 0 || ic < 0 || id < 0)
        return false;
    if (!(ia < ib && ia < ic && ib < id && ic < id))
        return false;

    // 下游：A 的下游 = {B, C, D}（顺序不定，只验包含）
    const QVector<GraphNode::Id> down = g.downstreamOf(a);
    if (down.size() != 3)
        return false;
    if (!down.contains(b) || !down.contains(c) || !down.contains(d))
        return false;

    // 加环 D→A：A→B→D→A，校验应失败
    g.connect(d, a, 0);
    QString cycleErr;
    if (g.validate(&cycleErr))
        return false;

    return true;
}

/** Stage 3a：求值、节点缓存命中、参数变更 → 级联失效 → 结果改变。 */
bool checkCache()
{
    QImage src(4, 4, QImage::Format_ARGB32_Premultiplied);
    src.fill(qRgba(100, 100, 100, 255));

    FilterNode node; // BrightnessContrast 默认
    node.setBrightness(0.2);

    PsGraph g;
    const GraphNode::Id s = g.addNode(std::make_shared<ImageSourceOp>(src));
    const GraphNode::Id f = g.addNode(std::make_shared<PointFilterOp>(node));
    g.connect(s, f, 0);

    const QRect rect(0, 0, 4, 4);
    const QImage r1 = g.evaluate(f, rect);
    if (r1.isNull())
        return false;
    if (!g.node(f)->cacheValid())
        return false; // 求值后应写入缓存

    const QImage r2 = g.evaluate(f, rect);
    if (r1 != r2)
        return false; // 二次求值（缓存命中）结果一致

    // 替换算子参数 + bumpVersion → 本节点与下游失效、结果改变
    FilterNode brighter;
    brighter.setBrightness(0.8);
    g.node(f)->setOp(std::make_shared<PointFilterOp>(brighter));
    g.bumpVersion(f);
    if (g.node(f)->cacheValid())
        return false; // 参数变更后缓存应失效

    const QImage r3 = g.evaluate(f, rect);
    if (r3 == r1)
        return false; // 亮度 0.2 → 0.8，结果应不同

    return true;
}

} // namespace

/** Stage 3b：合成图求值与直接合成逐像素一致（栅格层 + 混合 + 不透明度）。 */
bool checkCompositeGraph()
{
    auto doc = ImageDocument::createBlank(8, 8, Qt::white);

    QImage red(8, 8, QImage::Format_ARGB32_Premultiplied);
    red.fill(qRgba(255, 0, 0, 255));
    doc->placeImageAsLayer(red, QStringLiteral("red"));
    doc->setLayerOpacity(1, 0.5);

    QImage blue(8, 8, QImage::Format_ARGB32_Premultiplied);
    blue.fill(qRgba(0, 0, 255, 255));
    doc->placeImageAsLayer(blue, QStringLiteral("blue"));
    doc->setLayerBlendMode(2, BlendMode::Multiply);

    const QRect full(0, 0, 8, 8);
    const QImage direct = Compositor::composite(*doc);
    const QImage viaGraph = Compositor::compositeViaGraph(*doc, full);
    return !direct.isNull() && direct == viaGraph;
}

bool graphSelfCheck()
{
    return checkStructure() && checkCache() && checkCompositeGraph();
}

} // namespace Ps
