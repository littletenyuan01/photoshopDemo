/**
 * focusdabop.h — 聚焦工具 dab 缓冲算子（engine/op 层）。
 *
 * 模糊 / 锐化：局部盒模糊后按刷盖度混合；涂抹：沿笔画方向拖拽像素。
 * 对照 GIMP GimpConvolve / GimpSmudge 的单次 dab。
 */
#ifndef ENGINE_OP_FOCUSDABOP_H
#define ENGINE_OP_FOCUSDABOP_H

#include "bufferop.h"
#include "opname.h"
#include "engine/painttypes.h"

#include <QPointF>

namespace Ps {

class FocusDabOp : public BufferOp
{
public:
    FocusDabOp() = default;

    QString id() const override { return opNameId(OpName::FocusDab); }
    QString name() const override { return opNameTitle(OpName::FocusDab); }

    void setCenter(const QPointF &c) { m_center = c; }
    void setRadius(qreal r) { m_radius = r; }
    void setHardness(qreal h) { m_hardness = h; }
    void setStrength(qreal s) { m_strength = s; }
    void setMode(FocusMode m) { m_mode = m; }
    /** 涂抹：采样偏移（层内，通常 = 上一笔尖 − 当前笔尖）。 */
    void setSmudgeDelta(const QPointF &d) { m_smudgeDelta = d; }

    static QRect dabBounds(const QPointF &center, qreal radius);

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_center;
    qreal m_radius = 1.0;
    qreal m_hardness = 0.85;
    qreal m_strength = 0.5;       ///< 0..1 效果强度
    FocusMode m_mode = FocusMode::Blur;
    QPointF m_smudgeDelta;         ///< Smudge 专用
};

} // namespace Ps

#endif // ENGINE_OP_FOCUSDABOP_H
