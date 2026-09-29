/**
 * selectpolygonop.h — 多边形写入选区算子（engine/op 层）。
 *
 * 对照 GIMP gimp_channel_select_polygon → scan_convert 填充分路径再 combine。
 * 自由套索松手后把折线首尾闭合为多边形写入文档级 Selection。
 */
#ifndef ENGINE_OP_SELECTPOLYGONOP_H
#define ENGINE_OP_SELECTPOLYGONOP_H

#include "bufferop.h"
#include "domain/selection.h"
#include "opname.h"

#include <QPolygonF>
#include <QRect>

namespace Ps {

/**
 * 多边形选区算子（对照 gimp_channel_select_polygon）。
 *
 * - 输入：文档坐标折线（≥3 点）；松手时工具侧已视为闭合。
 * - 输出：按 ChannelOp 合并进 OpContext::selection。
 * - 硬边、无羽化（与本项目矩形/椭圆选区一致）。
 * - @return process 返回 shape mask 外接矩形（文档坐标）；供调用方知悉影响范围。
 */
class SelectPolygonOp : public BufferOp
{
public:
    SelectPolygonOp() = default;

    QString id() const override { return opNameId(OpName::SelectPolygon); }
    QString name() const override { return opNameTitle(OpName::SelectPolygon); }

    void setPoints(const QPolygonF &points) { m_points = points; }
    void setChannelOp(ChannelOp op) { m_op = op; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPolygonF m_points;                 ///< 文档坐标顶点（未强制闭合）
    ChannelOp m_op = ChannelOp::Replace; ///< 加/减/替/交
};

} // namespace Ps

#endif // ENGINE_OP_SELECTPOLYGONOP_H
