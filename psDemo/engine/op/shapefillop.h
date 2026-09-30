/**
 * shapefillop.h — 形状填充缓冲算子（engine/op 层）。
 *
 * 在活动层瓦片上绘制矩形/椭圆/三角形/直线（前景色填充与/或描边）。
 * 对照 GIMP 矢量路径栅格化的精简版：本 Demo 直接 QPainter 栅格化。
 */
#ifndef ENGINE_OP_SHAPEFILLOP_H
#define ENGINE_OP_SHAPEFILLOP_H

#include "bufferop.h"
#include "opname.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QPointF>
#include <QRectF>

namespace Ps {

class ShapeFillOp : public BufferOp
{
public:
    ShapeFillOp() = default;

    QString id() const override { return opNameId(OpName::ShapeFill); }
    QString name() const override { return opNameTitle(OpName::ShapeFill); }

    void setKind(ShapeKind k) { m_kind = k; }
    /** 形状包围盒（层内坐标）；直线用 topLeft→bottomRight 为端点。 */
    void setRect(const QRectF &r) { m_rect = r; }
    void setColor(const QColor &c) { m_color = c; }
    void setFill(bool on) { m_fill = on; }
    void setStroke(bool on) { m_stroke = on; }
    void setStrokeWidth(qreal w) { m_strokeWidth = w; }
    void setCornerRadius(qreal r) { m_cornerRadius = r; }
    void setAntialias(bool on) { m_antialias = on; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    ShapeKind m_kind = ShapeKind::Rect;
    QRectF m_rect;
    QColor m_color = Qt::black;
    bool m_fill = true;
    bool m_stroke = false;
    qreal m_strokeWidth = 2.0;
    qreal m_cornerRadius = 0.0;
    bool m_antialias = true;
};

} // namespace Ps

#endif // ENGINE_OP_SHAPEFILLOP_H
