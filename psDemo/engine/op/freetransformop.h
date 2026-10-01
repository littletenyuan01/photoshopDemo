/**
 * freetransformop.h — 自由变换缓冲算子（engine/op 层）。
 *
 * 源矩形 → 目标四边形：逆映射逐像素采样写入瓦片。
 * 对照 GIMP gimp_drawable_transform_* + GEGL sampler（nearest / linear / cubic）。
 */
#ifndef ENGINE_OP_FREETRANSFORMOP_H
#define ENGINE_OP_FREETRANSFORMOP_H

#include "bufferop.h"
#include "engine/painttypes.h"
#include "opname.h"

#include <QImage>
#include <QPointF>
#include <QRect>

namespace Ps {

class FreeTransformOp : public BufferOp
{
public:
    FreeTransformOp() = default;

    QString id() const override { return opNameId(OpName::FreeTransform); }
    QString name() const override { return opNameTitle(OpName::FreeTransform); }

    /**
     * 源内容（层内坐标）。若 @p sourcePixels 非空则直接用；
     * 否则从瓦片 @p sourceRect 抽取。
     */
    void setSource(const QRect &sourceRect, const QImage &sourcePixels = QImage())
    {
        m_sourceRect = sourceRect;
        m_sourcePixels = sourcePixels;
    }

    /**
     * 目标四边形（层内坐标，顺序：TL, TR, BR, BL）。
     * 与源矩形四角建立 quadToQuad 映射。
     */
    void setDestQuad(const QPointF corners[4])
    {
        for (int i = 0; i < 4; ++i)
            m_dest[i] = corners[i];
    }

    /** 是否先清空源矩形（会话已挖空时可关）。 */
    void setClearSource(bool on) { m_clearSource = on; }

    /** 插值：邻近 / 两次线性 / 两次立方（双三次 Keys/Catmull-Rom）。 */
    void setInterpolation(TransformInterpolation interp) { m_interpolation = interp; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;

private:
    QRect m_sourceRect;
    QImage m_sourcePixels;
    QPointF m_dest[4];
    bool m_clearSource = true;
    TransformInterpolation m_interpolation = TransformInterpolation::Bicubic;
};

} // namespace Ps

#endif // ENGINE_OP_FREETRANSFORMOP_H
