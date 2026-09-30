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
#include <QImage>
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

    /**
     * 仿制图章单次 dab。
     * @param center       目标 dab 中心（层内坐标）
     * @param sourceCenter 采样中心（文档坐标）
     * @param sample       文档尺寸预乘 ARGB（合成或单层）
     */
    static QRect cloneStampDab(TileBuffer &tiles,
                               const QPointF &center,
                               const QPointF &sourceCenter,
                               qreal radius,
                               const QImage &sample,
                               qreal hardness = 0.85,
                               qreal opacity = 1.0,
                               PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 沿 from→to 插值仿制笔画。
     * @param sourceFrom / sourceTo 与 from/to 同步移动的采样中心（文档坐标）
     */
    static QRect cloneStrokeSegment(TileBuffer &tiles,
                                    const QPointF &from,
                                    const QPointF &to,
                                    const QPointF &sourceFrom,
                                    const QPointF &sourceTo,
                                    qreal radius,
                                    const QImage &sample,
                                    qreal hardness = 0.85,
                                    qreal opacity = 1.0,
                                    qreal spacing = 0.25,
                                    PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 聚焦工具单次 dab（模糊 / 锐化 / 涂抹）。
     * @param smudgeDelta 涂抹采样偏移（层内）；Blur/Sharpen 忽略
     */
    static QRect focusDab(TileBuffer &tiles,
                          const QPointF &center,
                          qreal radius,
                          FocusMode mode,
                          qreal strength = 0.5,
                          qreal hardness = 0.85,
                          const QPointF &smudgeDelta = QPointF(),
                          PaintSelectionClip clip = PaintSelectionClip());

    /** 沿 from→to 插值聚焦笔画。 */
    static QRect focusStrokeSegment(TileBuffer &tiles,
                                    const QPointF &from,
                                    const QPointF &to,
                                    qreal radius,
                                    FocusMode mode,
                                    qreal strength = 0.5,
                                    qreal hardness = 0.85,
                                    qreal spacing = 0.25,
                                    PaintSelectionClip clip = PaintSelectionClip());

    /** 色调工具单次 dab（减淡 / 海绵）。 */
    static QRect toneDab(TileBuffer &tiles,
                         const QPointF &center,
                         qreal radius,
                         ToneMode mode,
                         qreal strength = 0.4,
                         qreal hardness = 0.85,
                         PaintSelectionClip clip = PaintSelectionClip());

    /** 沿 from→to 插值色调笔画。 */
    static QRect toneStrokeSegment(TileBuffer &tiles,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   ToneMode mode,
                                   qreal strength = 0.4,
                                   qreal hardness = 0.85,
                                   qreal spacing = 0.25,
                                   PaintSelectionClip clip = PaintSelectionClip());

    /**
     * 形状填充 / 描边（矩形·椭圆·三角·直线）。
     * @param rect 层内坐标；直线时 topLeft/bottomRight 为两端点
     */
    static QRect fillShape(TileBuffer &tiles,
                           ShapeKind kind,
                           const QRectF &rect,
                           const QColor &color,
                           bool fill,
                           bool stroke,
                           qreal strokeWidth = 2.0,
                           qreal cornerRadius = 0.0,
                           bool antialias = true,
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

    /**
     * 连通域/相似色写入选区（魔棒）。
     * 经 OpRunner → SelectFloodOp；对照 gimp_pickable_contiguous_region_*。
     * @param sample 文档尺寸预乘 ARGB 采样图（合成或单层）
     */
    static QRect selectFlood(Selection &selection,
                             const QImage &sample,
                             const QPoint &seed,
                             int tolerance,
                             bool contiguous,
                             ChannelOp op);
};

} // namespace Ps

#endif // PAINTENGINE_H
