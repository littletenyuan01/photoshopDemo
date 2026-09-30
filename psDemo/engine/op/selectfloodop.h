/**
 * selectfloodop.h — 连通域/相似色写入选区算子（engine/op 层）。
 *
 * 对照 GIMP Fuzzy Select：`gimp_pickable_contiguous_region_by_seed` /
 * `by_color` → channel combine。本算子在采样图上洪泛，结果合并进 Selection。
 */
#ifndef ENGINE_OP_SELECTFLOODOP_H
#define ENGINE_OP_SELECTFLOODOP_H

#include "bufferop.h"
#include "domain/selection.h"
#include "opname.h"

#include <QImage>
#include <QPoint>
#include <QRect>

namespace Ps {

/**
 * 选区洪泛算子（魔棒 / 快速选择共用）。
 *
 * - 采样图：文档尺寸预乘 ARGB（合成或单层贴到文档坐标）
 * - contiguous=true：四邻接 BFS；false：整图相似色（对照 by_color）
 * - 写入：shapeMask → Selection::combineShapeMask
 */
class SelectFloodOp : public BufferOp
{
public:
    SelectFloodOp() = default;

    QString id() const override { return opNameId(OpName::SelectFlood); }
    QString name() const override { return opNameTitle(OpName::SelectFlood); }

    void setSampleImage(const QImage &img) { m_sample = img; }
    void setSeed(const QPoint &seed) { m_seed = seed; }
    void setTolerance(int t) { m_tolerance = t; }
    void setContiguous(bool v) { m_contiguous = v; }
    void setChannelOp(ChannelOp op) { m_op = op; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;
    void finish(OpContext &ctx) override;

private:
    QImage m_sample; ///< 文档尺寸采样图（预乘）；configure 每次写入
    QPoint m_seed;
    int m_tolerance = 32;
    bool m_contiguous = true;
    ChannelOp m_op = ChannelOp::Replace;

    QImage m_region; ///< 洪泛掩码 Grayscale8（命中=255）
    int m_seedR = 0, m_seedG = 0, m_seedB = 0, m_seedA = 0;
    int m_tol = 0;
};

} // namespace Ps

#endif // ENGINE_OP_SELECTFLOODOP_H
