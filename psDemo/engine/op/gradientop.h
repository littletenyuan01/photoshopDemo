/**
 * gradientop.h — 渐变填充算子（engine/op 层）。
 *
 * 逐像素独立求值 + 预乘 SourceOver；工作窗口 = roi ∩ 选区外接框。
 * 对照 GIMP gimpoperationgradient。
 */
#ifndef ENGINE_OP_GRADIENTOP_H
#define ENGINE_OP_GRADIENTOP_H

#include "bufferop.h"
#include "opname.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QPointF>
#include <QRect>

namespace Ps {

/**
 * 渐变填充算子（对照 GIMP Blend/Gradient + gimpoperationgradient）。
 *
 * 逐像素独立求值（因子只依赖坐标），所以直接在「工作窗口」覆盖的瓦片上算因子、
 * SourceOver 写回，**不需要**底图 + overlay 两张整层临时图
 * （旧实现每张各占一个文档大小，且回写要 reset 全部瓦片）。
 */
class GradientOp : public BufferOp
{
public:
    GradientOp() = default;

    QString id() const override { return opNameId(OpName::Gradient); }
    QString name() const override { return opNameTitle(OpName::Gradient); }

    void setStart(const QPointF &p) { m_start = p; }
    void setEnd(const QPointF &p) { m_end = p; }
    void setForeground(const QColor &c) { m_fg = c; }
    void setBackground(const QColor &c) { m_bg = c; }
    void setType(GradientType t) { m_type = t; }
    void setOpacity(qreal o) { m_opacity = o; }
    void setOffsetPercent(int p) { m_offsetPercent = p; }
    void setReverse(bool v) { m_reverse = v; }
    void setDither(bool v) { m_dither = v; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_start;                      ///< 渐变起点（层内坐标）
    QPointF m_end;                        ///< 渐变终点（层内坐标）
    QColor m_fg = Qt::black;              ///< 前景色
    QColor m_bg = Qt::white;              ///< 背景色
    GradientType m_type = GradientType::Linear;
    qreal m_opacity = 1.0;                ///< 不透明度 0..1
    int m_offsetPercent = 0;              ///< GIMP 式偏移 0..100
    bool m_reverse = false;               ///< 交换 fg/bg
    bool m_dither = false;                ///< 是否加微量随机抖动

    QRect m_window;                       ///< 工作窗口（层内坐标，= roi ∩ 选区外接框 ∩ 层）
    qreal m_opacityClamped = 1.0;         ///< prepare 夹取后的不透明度
    int m_offsetClamped = 0;              ///< prepare 夹取后的偏移
    qreal m_dist = 0.0;                   ///< prepare 缓存：渐变轴长度或径向半径
    qreal m_vx = 0.0;                     ///< prepare 缓存：单位方向 X
    qreal m_vy = 0.0;                     ///< prepare 缓存：单位方向 Y
};

} // namespace Ps

#endif // ENGINE_OP_GRADIENTOP_H
