/**
 * clonestampdabop.h — 仿制图章 dab 缓冲算子（engine/op 层）。
 *
 * 从文档尺寸采样图按源中心取样，以圆形软边刷写入活动层瓦片。
 * 对照 GIMP GimpClone / paint-core dab；本 Demo 单次 dab 由工具插值成笔画。
 */
#ifndef ENGINE_OP_CLONESTAMPDABOP_H
#define ENGINE_OP_CLONESTAMPDABOP_H

#include "bufferop.h"
#include "opname.h"

#include <QImage>
#include <QPointF>

namespace Ps {

/**
 * 仿制图章单次 dab（对照 GIMP clone 单次 stamp）。
 * 采样图指针由调用方持有（一笔内复用）；坐标：中心为层内，源中心为文档。
 */
class CloneStampDabOp : public BufferOp
{
public:
    CloneStampDabOp() = default;

    QString id() const override { return opNameId(OpName::CloneStampDab); }
    QString name() const override { return opNameTitle(OpName::CloneStampDab); }

    void setCenter(const QPointF &c) { m_center = c; }
    void setSourceCenter(const QPointF &c) { m_sourceCenter = c; }
    void setRadius(qreal r) { m_radius = r; }
    void setHardness(qreal h) { m_hardness = h; }
    void setOpacity(qreal o) { m_opacity = o; }
    /** 文档尺寸预乘 ARGB 采样图；生命周期须覆盖本次 run。 */
    void setSample(const QImage *sample) { m_sample = sample; }

    /** dab 在层内坐标下的整数包围盒（含抗锯齿余量）。 */
    static QRect dabBounds(const QPointF &center, qreal radius);

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QPointF m_center;              ///< dab 中心（层内坐标）
    QPointF m_sourceCenter;        ///< 对应采样中心（文档坐标）
    qreal m_radius = 1.0;
    qreal m_hardness = 0.85;
    qreal m_opacity = 1.0;
    const QImage *m_sample = nullptr;
};

} // namespace Ps

#endif // ENGINE_OP_CLONESTAMPDABOP_H
