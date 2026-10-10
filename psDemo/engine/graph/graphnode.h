/**
 * graphnode.h — 图节点（engine/graph 层）。
 *
 * DAG 中的一个顶点：节点 id + 输入口（连向上游节点输出）+ 算子 + 输出缓存 + 版本。
 * 对照 GEGL GeglNode：节点持有 operation，输入 pad 连到上游节点输出，并缓存求值结果。
 */
#ifndef ENGINE_GRAPH_GRAPHNODE_H
#define ENGINE_GRAPH_GRAPHNODE_H

#include "graphop.h"

#include <QImage>
#include <QRect>
#include <QVector>
#include <QtGlobal>
#include <memory>

namespace Ps {

/**
 * 图节点（对照 GeglNode 的结构语义，不含 GEGL 的 GObject/属性体系）。
 *
 * 【构成】一个算子（GraphOp）+ 若干输入口 + 输出缓存 + 参数版本。
 * 输入口 i 的值 = 上游节点 id，表示「本节点的第 i 个输入取自该上游节点的输出」。
 * 数据流 = 上游 → 下游。
 *
 * 【缓存】evaluate 结果按（区域）缓存；版本变更或上游失效时置无效。
 * 见 PsGraph::evaluate / bumpVersion / invalidateFrom。
 */
class GraphNode
{
public:
    using Id = int;
    /** 未连接 / 无效节点哨兵。 */
    static constexpr Id kInvalidId = -1;

    GraphNode() = default;
    explicit GraphNode(Id id) : m_id(id) {}

    Id id() const { return m_id; }
    void setId(Id id) { m_id = id; }

    // —— 算子 ——
    /** 挂算子（纯函数）。改参数后调用方须 bumpVersion 通知下游失效。 */
    void setOp(std::shared_ptr<GraphOp> op) { m_op = std::move(op); }
    GraphOp *op() const { return m_op.get(); }

    // —— 输入口 ——
    /** 输入口个数（= 已声明输入口数，含未连接的口）。 */
    int inputCount() const { return m_inputs.size(); }

    /** 第 index 个输入口连到的上游节点 id；越界或未连接返回 kInvalidId。 */
    Id input(int index) const
    {
        return (index >= 0 && index < m_inputs.size()) ? m_inputs[index] : kInvalidId;
    }

    /** 连接第 index 个输入口到上游节点；中间缺口以 kInvalidId 填充。 */
    void setInput(int index, Id nodeId)
    {
        if (index < 0)
            return;
        while (m_inputs.size() <= index)
            m_inputs.append(kInvalidId);
        m_inputs[index] = nodeId;
    }

    /** 全部输入口（按下标序）快照。 */
    const QVector<Id> &inputs() const { return m_inputs; }

    // —— 输出缓存 ——
    bool cacheValid() const { return m_cacheValid; }
    const QImage &cacheImage() const { return m_cacheImage; }
    QRect cacheRegion() const { return m_cacheRegion; }
    /** 写入缓存（隐式置有效）。 */
    void setCache(const QImage &image, const QRect &region)
    {
        m_cacheImage = image;
        m_cacheRegion = region;
        m_cacheValid = true;
    }
    void invalidateCache()
    {
        m_cacheValid = false;
        m_cacheImage = QImage(); ///< 释放共享像素
    }

    // —— 版本 / 脏 ——
    int version() const { return m_version; }
    /** 参数变更后调用：版本 +1 并失效本节点缓存（下游由 PsGraph::bumpVersion 级联）。 */
    void bumpVersion()
    {
        ++m_version;
        invalidateCache();
    }

private:
    Id m_id = kInvalidId;                 ///< 节点 id（在 PsGraph 内 = 下标）
    std::shared_ptr<GraphOp> m_op;        ///< 算子（纯函数，无副作用）
    QVector<Id> m_inputs;                 ///< 输入口 → 上游节点 id（kInvalidId = 未连接）
    QImage m_cacheImage;                  ///< 输出缓存（预乘 ARGB32）
    QRect m_cacheRegion;                  ///< 缓存覆盖区域
    bool m_cacheValid = false;            ///< 缓存有效位
    int m_version = 0;                    ///< 参数版本；变更 +1
};

} // namespace Ps

#endif // ENGINE_GRAPH_GRAPHNODE_H
