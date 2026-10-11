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

bool FilterStack::hasGeometryOp() const
{
    // 「必须整幅求值一次」的几何算子：**只有环绕位移**。
    // 它是**多矩形**依赖（输出最左边要读输入最右边），单矩形根本表达不了；
    // 而非环绕位移（现在的默认）的输入需求是**单矩形**（输出区 ∪ 平移后的输出区），
    // 可以逐块求值 —— 见 FilterNode::requiredInputs。
    // 判据写成「按算子名 + 看环绕开关」而不是「看 pad」：面积算子也有 pad，但它们可以逐块求值。
    // 新增几何算子时必须加到这里（否则会被当成可逐块求值 → 块界错位）。
    for (const FilterNode &n : m_nodes) {
        if (n.isEnabled() && n.op() == OpName::Offset && n.offsetWrap())
            return true;
    }
    return false;
}

bool FilterStack::containsOffset() const
{
    // 条带并行对位移不适用：位移的需求是**平移型**（不是对称外扩），条带边界公式不成立。
    for (const FilterNode &n : m_nodes) {
        if (n.isEnabled() && n.op() == OpName::Offset)
            return true;
    }
    return false;
}

bool FilterStack::hasPositionalOp() const
{
    // 目前唯一的位置相关算子是「添加杂色」：输出依赖像素的**绝对位置**（噪声是位置的函数）。
    // 与 hasGeometryOp 一样按算子点名（不看 pad —— 它 pad 就是 0），新增时必须加到这里。
    for (const FilterNode &n : m_nodes) {
        if (n.isEnabled() && n.op() == OpName::AddNoise)
            return true;
    }
    return false;
}

QRect FilterStack::requiredInputRect(const QRect &output) const
{
    // 见头文件：自栈顶往栈底倒推（栈按顺序作用，后一个算子的输入需求 = 前一个的输出需求）。
    QRect r = output;
    for (int i = m_nodes.count() - 1; i >= 0; --i) {
        const FilterNode &n = m_nodes.at(i);
        if (!n.isEnabled())
            continue;
        r = n.requiredInputs(r);
    }
    return r;
}

/** 对 source 拷贝自底向顶应用已启用节点；无启用节点时原样返回。 */
QImage FilterStack::apply(const QImage &source) const
{
    return apply(source, nullptr);
}

/**
 * 带协作式取消的求值：谓词返回 true 时**尽早停下**，返回的是半成品。
 *
 * 契约与 `FilterEval::applyNode(image, node, shouldAbort)` 一致：调用方必须丢弃结果。
 * 谓词注入图里的每个 `PointFilterOp`（图是本次现建的，故按值写入是安全的）。
 */
QImage FilterStack::apply(const QImage &source, const std::function<bool()> &shouldAbort) const
{
    return apply(source, source.rect(), source.rect(), shouldAbort);
}

/**
 * 带协作式取消 + **几何**的求值（ROI 通用化第 ① 步）。
 *
 * @param srcRect / @p outRect 用**文档坐标**（约定见 docs/engine/roi.md §5 的迁移设计）。
 * 这与 `Layer::compositeRasterRegion` 里 want/srcWant 的坐标空间一致，调用方无需换算；
 * 本函数负责把它们平移到**输入图本地坐标**（`FilterContext` 的几何一律用本地坐标：
 * 输入图左上角 = 原点，外扩那一圈落在正区间）。
 */
QImage FilterStack::apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                          const std::function<bool()> &shouldAbort) const
{
    if (source.isNull() || !hasEnabled())
        return source;

    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 几何缺省 = 整张图（本地坐标语义）
    const QRect srcIn = srcRect.isNull() ? out.rect() : srcRect;
    const QRect dstIn = outRect.isNull() ? out.rect() : outRect;
    // 坐标空间说明：`srcRect`/`outRect` 是**文档坐标**（调用方约定），而 `FilterContext`
    // 的几何一律用「输入图本地坐标」—— 输入图就是 srcRect 抽出来的那块，故本地原点 = srcRect.topLeft()。
    // 换算由 `PointFilterOp` 在把几何交给 `applyNode` 时完成（它知道手里每张图的外框）。
    Q_ASSERT_X(out.rect().contains(srcIn.translated(-srcIn.topLeft()))
                   && out.rect().contains(dstIn.translated(-srcIn.topLeft())),
               "FilterStack::apply",
               "srcRect/outRect（文档坐标）的尺寸必须与输入图自洽，且 srcRect 包住 outRect");

    // 建链图：source → f1 → f2 → …（对照 GimpFilterStack 的 GeglNode *graph）
    // 点滤镜 ROI 恒等，逐像素结果与旧循环一致；图求值在此即「线性链的特例」。
    PsGraph graph;
    GraphNode::Id prev = graph.addNode(std::make_shared<ImageSourceOp>(out));
    for (const FilterNode &node : m_nodes) {
        if (!node.isEnabled())
            continue;
        if (shouldAbort && shouldAbort())
            return QImage(); // 已取消：交回空图，调用方丢弃
        auto op = std::make_shared<PointFilterOp>(node);
        op->setShouldAbort(shouldAbort);
        op->setGeometry(srcIn, dstIn);
        const GraphNode::Id cur = graph.addNode(op);
        graph.connect(prev, cur, 0);
        prev = cur;
    }
    return graph.evaluate(prev, out.rect());
}

} // namespace Ps
