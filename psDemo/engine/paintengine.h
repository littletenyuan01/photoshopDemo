/**
 * paintengine.h — 像素绘制门面（engine 层）。
 *
 * 工具/domain 的稳定 API；内部组装 OpContext 并经 OpRunner 调度缓冲算子。
 * 返回层内坐标脏矩形，供 markDirty 驱动投影增量 sync。
 */
#ifndef PAINTENGINE_H
#define PAINTENGINE_H

#include "domain/selection.h"
#include "engine/paintselectionclip.h"
#include "engine/painttypes.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
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

    /** 油漆桶洪泛；@return 层内坐标脏矩形。 */
    static QRect floodFill(TileBuffer &tiles,
                           const QPoint &seed,
                           const QColor &fillColor,
                           int tolerance,
                           bool contiguous,
                           PaintSelectionClip clip = PaintSelectionClip());

    /** 渐变填充；@return 层内坐标脏矩形。 */
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

    /**
     * 多边形写入选区（自由套索 / 多边形套索共用）。
     * 经 OpRunner → SelectPolygonOp；对照 gimp_channel_select_polygon。
     * @return 影响区域（文档坐标）；点数不足时空矩形。
     */
    static QRect selectPolygon(Selection &selection,
                               const QPolygonF &points,
                               ChannelOp op);
};

} // namespace Ps

#endif // PAINTENGINE_H
