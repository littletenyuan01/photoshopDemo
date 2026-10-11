/**
 * filterstack.h — 图层滤镜栈：增删开关，对临时图自底向顶求值（domain 层）。
 */
#ifndef DOMAIN_FILTERSTACK_H
#define DOMAIN_FILTERSTACK_H

#include "filternode.h"

#include <QImage>
#include <QRect>
#include <QVector>

#include <functional>

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

    /**
     * 同上，但带**协作式取消**：@p shouldAbort 返回 true 时尽早停下。
     *
     * @warning 取消属于「中途放弃」：返回值可能是**半成品或空图**，调用方必须丢弃
     * （不要写进缓存、不要显示）。谓词会被注入图里每个节点，面积算子在自己的行循环里
     * 周期性查询它 —— 见 engine/op/filterop.h 的 `FilterContext::shouldAbort`。
     */
    QImage apply(const QImage &source, const std::function<bool()> &shouldAbort) const;

    /**
     * 同上，且把 ROI **几何**一并交给每个节点（ROI 通用化第 ① 步）。
     *
     * @param srcRect 本次求值的**所需输入区**（**文档坐标**）
     * @param outRect 本次求值的**请求输出区**（同坐标系，须被 srcRect 包住）
     *
     * 调用场景：像素层滤镜按「want 外扩 pad」取输入（见 `Layer::compositeRasterRegion`），
     * 此时 `srcRect` 比 `outRect` 大一圈 pad，面积算子据此知道多出来的边在哪。
     * 坐标空间选**文档坐标**是刻意与 [roi.md](../docs/engine/roi.md) §5 的迁移设计一致
     * （调用方不必换算）；函数内部再平移到输入图本地坐标交给算子（见 `FilterContext` 注释）。
     * 不传（用上面两条重载）= 两者都退化为 `source.rect()`。
     */
    QImage apply(const QImage &source, const QRect &srcRect, const QRect &outRect,
                 const std::function<bool()> &shouldAbort) const;

    /** 是否存在至少一个已启用节点（合成快速路径用）。 */
    bool hasEnabled() const;

    /**
     * 栈里是否有**几何算子**（如位移）：这类算子的输出像素读「别处」的输入，
     * 逐块求值会在块界错位，**必须整幅求值一次**。
     *
     * 调用方（`Layer::compositeRasterRegion`）据此退化为「整层求值 + 裁请求区」，
     * 与「有启用样式（需要外扩 halo）」走整层缓存是同一类取舍。理由见
     * docs/engine/roi.md §5「②③ 为什么后置」。
     */
    bool hasGeometryOp() const;

    /**
     * 所有**已启用**节点的输入外扩之和（面积算子半径之和；点算子为 0）。
     * 调用方若只想算某矩形，须先取「该矩形外扩 totalPad()」再求值、最后裁回。
     */
    int totalPad() const
    {
        int p = 0;
        for (const FilterNode &n : m_nodes)
            if (n.isEnabled())
                p += n.inputPad();
        return p;
    }

    /** 整栈快照 / 替换（对话框确认、属性 undo）。 */
    QVector<FilterNode> snapshot() const { return m_nodes; }
    void replaceAll(const QVector<FilterNode> &nodes) { m_nodes = nodes; }

private:
    QVector<FilterNode> m_nodes; ///< 自底向顶滤镜节点列表
};

} // namespace Ps

#endif // DOMAIN_FILTERSTACK_H
