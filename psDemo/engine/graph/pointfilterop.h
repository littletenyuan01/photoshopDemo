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

    QVector<QRect> requiredInputs(const QRect &out) const override
    {
        // 面积算子外扩输入 ROI（对照 GeglOperationFilter::get_required_for_output）；
        // 点算子 inputPad()==0，退化为 ROI 恒等。
        const int p = m_node.inputPad();
        return { out.adjusted(-p, -p, p, p) };
    }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        QImage result = ins.value(0);
        if (!result.isNull()) {
            FilterEval::applyNode(result, m_node);
            // 面积算子：ins[0] 覆盖 out 外扩 pad，求值后裁回 out。
            // 按对称外扩取中心（要求图未在文档边界裁剪输入，内部区域恒成立）。
            const int p = m_node.inputPad();
            if (p > 0 && result.size() != out.size()) {
                const int dx = (result.width() - out.width()) / 2;
                const int dy = (result.height() - out.height()) / 2;
                result = result.copy(dx, dy, out.width(), out.height());
            }
        }
        return result;
    }

    /** 点滤镜可就地；面积算子需要裁回、且输入比输出大，故不走就地快捷路径。 */
    bool canProcessInPlace() const override { return m_node.inputPad() == 0; }

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
