#ifndef DOMAIN_FILTERSTACK_H
#define DOMAIN_FILTERSTACK_H

#include "filternode.h"

#include <QImage>
#include <QVector>

namespace Ps {

/**
 * 图层滤镜栈（对照 GimpFilterStack / drawable filters）。
 * 自底向顶对临时图求值；**绝不**写回 Layer 瓦片。
 */
class FilterStack
{
public:
    bool isEmpty() const { return m_nodes.isEmpty(); }
    int count() const { return m_nodes.size(); }

    const FilterNode &at(int index) const { return m_nodes.at(index); }
    FilterNode &at(int index) { return m_nodes[index]; }

    /** @return 新节点下标。 */
    int append(const FilterNode &node);
    bool removeAt(int index);
    bool setEnabled(int index, bool on);

    /** 对 @p source 的拷贝依次应用已启用节点；source 本身不改。 */
    QImage apply(const QImage &source) const;

    bool hasEnabled() const;

private:
    QVector<FilterNode> m_nodes;
};

} // namespace Ps

#endif // DOMAIN_FILTERSTACK_H
