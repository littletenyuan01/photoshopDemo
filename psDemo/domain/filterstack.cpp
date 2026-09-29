#include "filterstack.h"

#include "engine/filtereval.h"

namespace Ps {

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

QImage FilterStack::apply(const QImage &source) const
{
    if (source.isNull() || !hasEnabled())
        return source;

    QImage out = source;
    if (out.format() != QImage::Format_ARGB32_Premultiplied)
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    for (const FilterNode &node : m_nodes) {
        if (!node.isEnabled())
            continue;
        FilterEval::applyNode(out, node);
    }
    return out;
}

} // namespace Ps
