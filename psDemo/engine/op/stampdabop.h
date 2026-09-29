#ifndef ENGINE_OP_STAMPDABOP_H
#define ENGINE_OP_STAMPDABOP_H

#include "bufferop.h"
#include "opname.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QPointF>

namespace Ps {

/**
 * 圆形 dab 算子（对照 GIMP paint core 单次 stamp）。
 * 无大块临时缓冲时 prepare 只做校验；像素在 process 中直接写入瓦片。
 */
class StampDabOp : public BufferOp
{
public:
    StampDabOp() = default;

    QString id() const override { return opNameId(OpName::StampDab); }
    QString name() const override { return opNameTitle(OpName::StampDab); }

    void setCenter(const QPointF &c) { m_center = c; }
    void setRadius(qreal r) { m_radius = r; }
    void setColor(const QColor &c) { m_color = c; }
    void setMode(PaintMode m) { m_mode = m; }
    void setHardness(qreal h) { m_hardness = h; }

    static QRect dabBounds(const QPointF &center, qreal radius);

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_center;
    qreal m_radius = 1.0;
    QColor m_color = Qt::black;
    PaintMode m_mode = PaintMode::Paint;
    qreal m_hardness = 0.85;
};

} // namespace Ps

#endif // ENGINE_OP_STAMPDABOP_H
