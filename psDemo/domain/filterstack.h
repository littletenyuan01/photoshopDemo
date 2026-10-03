/**
 * filterstack.h — 图层滤镜栈：增删开关，对临时图自底向顶求值（domain 层）。
 */
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
    /** 栈内节点个数（自底向顶顺序）。 */
    int count() const { return m_nodes.size(); }

    const FilterNode &at(int index) const { return m_nodes.at(index); }
    FilterNode &at(int index) { return m_nodes[index]; }

    /** @return 新节点下标。 */
    int append(const FilterNode &node);
    /** 移除指定下标节点；越界返回 false。 */
    bool removeAt(int index);
    /** 开关指定节点；越界返回 false。 */
    bool setEnabled(int index, bool on);

    /** 对 @p source 的拷贝依次应用已启用节点；source 本身不改。 */
    QImage apply(const QImage &source) const;

    /** 是否存在至少一个已启用节点（合成快速路径用）。 */
    bool hasEnabled() const;

    /** 整栈快照 / 替换（对话框确认、属性 undo）。 */
    QVector<FilterNode> snapshot() const { return m_nodes; }
    void replaceAll(const QVector<FilterNode> &nodes) { m_nodes = nodes; }

private:
    QVector<FilterNode> m_nodes; ///< 自底向顶滤镜节点列表
};

} // namespace Ps

#endif // DOMAIN_FILTERSTACK_H
