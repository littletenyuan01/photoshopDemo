/**
 * pointfilterop.h — 点滤镜图算子（engine/graph 层）。
 *
 * 把 FilterEval::applyNode 的逐像素滤镜包成 GraphOp；ROI 恒等。
 * 对照 GEGL GeglOperationPointFilter。
 */
#ifndef ENGINE_GRAPH_POINTFILTEROP_H
#define ENGINE_GRAPH_POINTFILTEROP_H

#include "graphop.h"

#include "domain/filternode.h"
#include "engine/filtereval.h"

#include <utility>

namespace Ps {

/**
 * 点滤镜算子（对照 GeglOperationPointFilter）。
 * 参数 = 一个 FilterNode；process 对输入整区逐像素应用该滤镜。
 */
class PointFilterOp : public GraphOp
{
public:
    explicit PointFilterOp(const FilterNode &node) : m_node(node) {}

    int inputCount() const override { return 1; }

    QVector<QRect> requiredInputs(const QRect &out) const override { return { out }; }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        Q_UNUSED(out);
        QImage result = ins.value(0);
        if (!result.isNull())
            FilterEval::applyNode(result, m_node);
        return result;
    }

    /** 逐像素滤镜 → 可就地改输入（滤镜链上免每节点整图拷贝）。 */
    bool canProcessInPlace() const override { return true; }

    QImage processInPlace(QVector<QImage> &&ins, const QRect &out) const override
    {
        Q_UNUSED(out);
        QImage result = std::move(ins[0]); // 移走句柄（非深拷贝）
        if (!result.isNull())
            FilterEval::applyNode(result, m_node);
        return result;
    }

    const FilterNode &node() const { return m_node; }

private:
    FilterNode m_node; ///< 滤镜参数（只读）
};

} // namespace Ps

#endif // ENGINE_GRAPH_POINTFILTEROP_H
