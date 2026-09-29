#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include "engine/paintselectionclip.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>

namespace Ps {

class TileBuffer;

/**
 * 像素绘制引擎门面（开闭：对外稳定 API；新算法以 op 扩展，不改工具侧）。
 * 工具 / domain / UI 只依赖本头与 painttypes；具体算子仅在 .cpp 内可见。
 */
class PaintEngine
{
public:
    using Mode = PaintMode;
    using SelectionClip = PaintSelectionClip;
    using GradientType = ::Ps::GradientType;

    /** @return 层内坐标脏矩形（未改像素时空）。 */
    static QRect stampDab(TileBuffer &tiles,
                          const QPointF &center,
                          qreal radius,
                          const QColor &color,
                          Mode mode,
                          qreal hardness = 0.85,
                          PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 沿 from→to 插值连续盖 dab。
     * @return 本段所有 dab 的**层内脏矩形并集**；空 = 未改像素。
     *         调用方不再自行推算脏区（算错过一次：见 tools/painttool.cpp）。
     */
    static QRect strokeSegment(TileBuffer &tiles,
                               const QPointF &from,
                               const QPointF &to,
                               qreal radius,
                               const QColor &color,
                               Mode mode,
                               qreal hardness = 0.85,
                               qreal spacing = 0.25,
                               PaintSelectionClip clip = PaintSelectionClip());

    static QRect floodFill(TileBuffer &tiles,
                           const QPoint &seed,
                           const QColor &fillColor,
                           int tolerance,
                           bool contiguous,
                           PaintSelectionClip clip = PaintSelectionClip());

    static QRect fillGradient(TileBuffer &tiles,
                               const QPointF &start,
                               const QPointF &end,
                               const QColor &foreground,
                               const QColor &background,
                               GradientType type,
                               qreal opacity,
                               int offsetPercent,
                               bool reverse,
                               bool dither,
                               PaintSelectionClip clip = PaintSelectionClip());

    /** 实色 / 透明填充（编辑→填充 / 清除）。 */
    static QRect solidFill(TileBuffer &tiles,
                           const QColor &color,
                           PaintSelectionClip clip = PaintSelectionClip());
};

} // namespace Ps

#endif // PAINTENGINE_H
