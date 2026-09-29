#ifndef ENGINE_OP_FLOODFILLOP_H
#define ENGINE_OP_FLOODFILLOP_H

#include "bufferop.h"
#include "opname.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRgb>

namespace Ps {

/**
 * 油漆桶洪泛填充算子（对照 GIMP contiguous-region + apply_buffer）。
 *
 * 【为什么保留一份窗口副本】洪泛要随机访问「已到达区域的邻居」，且必须与 GIMP 一致：
 * 洪水在整层上蔓延，**之后**才按选区裁（GIMP 也是先算 region 再 apply_buffer 遮罩）。
 * 所以工作窗口只取 roiWindow（**不**按选区外接框截断），否则绕过障碍的路径会被切断。
 * prepare 物化该窗口，process 洪泛，finish 释放。
 */
class FloodFillOp : public BufferOp
{
public:
    FloodFillOp() = default;

    QString id() const override { return opNameId(OpName::FloodFill); }
    QString name() const override { return opNameTitle(OpName::FloodFill); }

    void setSeed(const QPoint &seed) { m_seed = seed; }
    void setFillColor(const QColor &c) { m_fillColor = c; }
    void setTolerance(int t) { m_tolerance = t; }
    void setContiguous(bool v) { m_contiguous = v; }

protected:
    bool prepare(OpContext &ctx) override;
    QRect process(OpContext &ctx) override;
    void finish(OpContext &ctx) override;

private:
    QPoint m_seed;
    QColor m_fillColor = Qt::black;
    int m_tolerance = 0;
    bool m_contiguous = true;

    QRect m_window;  ///< 工作窗口（层内坐标，= roi ∩ 层）；也是 m_work / m_region 的原点
    QImage m_work;   ///< 仅窗口大小的层像素副本
    /** 单张三态掩码（同窗口尺寸，Grayscale8）：0 = 未到达，1 = 已入队，255 = 命中。 */
    QImage m_region;
    QRgb m_fillPx = 0;
    int m_seedR = 0, m_seedG = 0, m_seedB = 0, m_seedA = 0;
    int m_tol = 0;
};

} // namespace Ps

#endif // ENGINE_OP_FLOODFILLOP_H
