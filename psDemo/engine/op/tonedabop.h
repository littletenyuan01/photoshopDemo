/**
 * tonedabop.h — 色调工具 dab 缓冲算子（engine/op 层）。
 *
 * 减淡：提亮；海绵：提高饱和度。对照 GIMP Dodge/Burn / Sponge 的精简子集。
 */
#ifndef ENGINE_OP_TONEDABOP_H
#define ENGINE_OP_TONEDABOP_H

#include "bufferop.h"
#include "opname.h"
#include "engine/painttypes.h"

#include <QPointF>

namespace Ps {

class ToneDabOp : public BufferOp
{
public:
    ToneDabOp() = default;

    QString id() const override { return opNameId(OpName::ToneDab); }
    QString name() const override { return opNameTitle(OpName::ToneDab); }

    void setCenter(const QPointF &c) { m_center = c; }
    void setRadius(qreal r) { m_radius = r; }
    void setHardness(qreal h) { m_hardness = h; }
    void setStrength(qreal s) { m_strength = s; }
    void setMode(ToneMode m) { m_mode = m; }

    static QRect dabBounds(const QPointF &center, qreal radius);

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_center;
    qreal m_radius = 1.0;
    qreal m_hardness = 0.85;
    qreal m_strength = 0.4;
    ToneMode m_mode = ToneMode::Dodge;
};

} // namespace Ps

#endif // ENGINE_OP_TONEDABOP_H
