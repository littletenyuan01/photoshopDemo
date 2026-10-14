/**
 * filterstack.cpp — FilterStack 节点管理与后端调度求值（domain 层）。
 */
#include "filterstack.h"

#include "engine/backend/rasterbackend.h"
#include "engine/backend/rasterscheduler.h"

namespace Ps {

/** 追加到栈顶，返回新下标；已满返回 -1。 */
int FilterStack::append(const FilterNode &node)
{
    if (m_nodes.size() >= kMaxFilters)
        return -1;
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

bool FilterStack::containsOffset() const
{
    // 条带并行对位移不适用：位移的需求是**平移型**（输出区 ∪ 平移后的输出区），
    // 不是「对称外扩」，条带的边界公式（band ± pad）对它不成立。
    for (const FilterNode &n : m_nodes) {
        if (n.isEnabled() && n.op() == OpName::Offset)
            return true;
    }
    return false;
}

bool FilterStack::hasPositionalOp() const
{
    // 位置相关：输出依赖像素的**绝对位置**（非整块平移不变）。
    // 添加杂色：噪声是 (x,y,seed) 的函数；波纹：相位用绝对 y（sin(2πy/λ)）。
    // 按算子点名；新增时必须加到这里。
    for (const FilterNode &n : m_nodes) {
        if (!n.isEnabled())
            continue;
        // Wind 交错用绝对行奇偶选方向（A24）：条带并行会相位错乱
        if (n.op() == OpName::AddNoise || n.op() == OpName::Ripple
            || n.op() == OpName::Mosaic || n.op() == OpName::RadialBlur
            || n.op() == OpName::Tiles || n.op() == OpName::Wind)
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
 */
QImage FilterStack::apply(const QImage &source, const std::function<bool()> &shouldAbort) const
{
    return apply(source, source.rect(), source.rect(), shouldAbort);
}

/**
 * 带协作式取消 + **几何**的求值；实际求值走 RasterScheduler（gpu-plan.md §3.2）。
 */
QImage FilterStack::apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                          const std::function<bool()> &shouldAbort) const
{
    if (source.isNull() || !hasEnabled())
        return source;

    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    ApplyStackResult r = ApplyStackResult::Ok;
    RasterScheduler::instance().runStack(out, *this, srcRect, outRect, shouldAbort, &r);
    if (r == ApplyStackResult::Cancelled)
        return QImage();
    return out;
}

} // namespace Ps
