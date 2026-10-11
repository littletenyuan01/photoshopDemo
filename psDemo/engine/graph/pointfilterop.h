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

#include <functional>
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
            // 见 toLocalOutput()：`m_srcRect`/`m_outRect` 是**文档坐标**，
            // 手里这张图（每个样本一个图）以它的左上角为本地原点。
            const QPoint origin = m_srcRect.topLeft();
            FilterEval::applyNode(result, m_node, m_srcRect.translated(-origin),
                                  m_outRect.translated(-origin), m_shouldAbort);
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
        if (!result.isNull()) {
            const QPoint origin = m_srcRect.topLeft();
            FilterEval::applyNode(result, m_node, m_srcRect.translated(-origin),
                                  m_outRect.translated(-origin), m_shouldAbort);
        }
        return result;
    }

    const FilterNode &node() const { return m_node; }

    /**
     * 协作式取消谓词（见 engine/op/filterop.h 的 FilterContext::shouldAbort）。
     *
     * 【为什么是可变成员 + 由调用方 set】`process()` 签名是 `const`（图算子是纯函数），
     * 而取消谓词属于**本次求值**的上下文、不属于算子参数。图节点由 `PsGraph` 内部持有，
     * 没有「每节点上下文」的口子；这里用一个只在**求值前**写入的小字段绕开，
     * 代价是同一个 `PointFilterOp` 实例不能并发求值 —— 本项目的图是**每次调用现建**的
     * （见 FilterStack::apply），故不成立共享。
     */
    void setShouldAbort(std::function<bool()> fn) { m_shouldAbort = std::move(fn); }

    /**
     * 本次求值的 ROI 几何（见 FilterContext 的坐标说明），**文档坐标**：
     * `srcRect` = 所需输入区（= `requiredInputs(out)` 那块）、`outRect` = 要产出的输出区。
     * 空 = 退化为整张图（本地坐标语义）。
     *
     * 与 `setShouldAbort` 同理：属于「本次求值」的上下文而非算子参数，故在求值前写入。
     */
    void setGeometry(const QRect &srcRect, const QRect &outRect)
    {
        m_srcRect = srcRect;
        m_outRect = outRect;
    }

private:
    FilterNode m_node; ///< 滤镜参数（只读）
    std::function<bool()> m_shouldAbort; ///< 本次求值的取消谓词（可为空）
    QRect m_srcRect; ///< 所需输入区（文档坐标；空 = 整张图）
    QRect m_outRect; ///< 请求输出区（文档坐标；空 = 整张图）
};

} // namespace Ps

#endif // ENGINE_GRAPH_POINTFILTEROP_H
