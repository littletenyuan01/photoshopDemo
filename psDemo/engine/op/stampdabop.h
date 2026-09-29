/**
 * stampdabop.h — 圆形 dab 缓冲算子（engine/op 层）。
 *
 * 画笔/橡皮单次 stamp；有选区时子区备份 + restoreOutsideSelection。
 * 对照 GIMP paint core dab。
 */
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

    /** dab 在层内坐标下的整数包围盒（含抗锯齿余量）。 */
    static QRect dabBounds(const QPointF &center, qreal radius);

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_center;                    ///< dab 中心（层内坐标）
    qreal m_radius = 1.0;                ///< 半径（像素）
    QColor m_color = Qt::black;          ///< 画笔色（Erase 模式忽略）
    PaintMode m_mode = PaintMode::Paint; ///< Paint 或 Erase
    qreal m_hardness = 0.85;             ///< 0=全软边，1=硬芯
};

} // namespace Ps

#endif // ENGINE_OP_STAMPDABOP_H
