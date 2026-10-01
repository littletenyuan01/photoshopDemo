/**
 * paintengine.cpp — paintengine.h 实现（engine 层）。
 *
 * dab / 洪泛 / 渐变 / 实色填充 / 多边形选区均走 OpRunner::run；
 * strokeSegment 沿路径插值多 dab。
 */
#include "paintengine.h"

#include "domain/tilebuffer.h"
#include "engine/op/clonestampdabop.h"
#include "engine/op/floodfillop.h"
#include "engine/op/focusdabop.h"
#include "engine/op/gradientop.h"
#include "engine/op/opcontext.h"
#include "engine/op/opname.h"
#include "engine/op/oprunner.h"
#include "engine/op/selectfloodop.h"
#include "engine/op/selectpolygonop.h"
#include "engine/op/shapefillop.h"
#include "engine/op/freetransformop.h"
#include "engine/op/solidfillop.h"
#include "engine/op/stampdabop.h"
#include "engine/op/tonedabop.h"

#include <QtMath>

namespace Ps {

namespace {

/** 脏区并集：空矩形不参与（QRect 的 empty/null 语义会让并集虚增边界）。 */
QRect unitedDirty(const QRect &a, const QRect &b)
{
    if (b.isEmpty())
        return a;
    return a.isEmpty() ? b : a.united(b);
}

} // namespace

QRect PaintEngine::stampDab(TileBuffer &tiles,
                            const QPointF &center,
                            qreal radius,
                            const QColor &color,
                            Mode mode,
                            qreal hardness,
                            PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::StampDab, ctx, [&](BufferOp &base) {
        auto &op = static_cast<StampDabOp &>(base);
        op.setCenter(center);
        op.setRadius(radius);
        op.setColor(color);
        op.setMode(mode);
        op.setHardness(hardness);
    });
}

QRect PaintEngine::strokeSegment(TileBuffer &tiles,
                                 const QPointF &from,
                                 const QPointF &to,
                                 qreal radius,
                                 const QColor &color,
                                 Mode mode,
                                 qreal hardness,
                                 qreal spacing,
                                 PaintSelectionClip clip)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6)
        return stampDab(tiles, to, radius, color, mode, hardness, clip);

    // 一笔里每个 dab 都是一次算子调度（实例常驻，见 OpRunner::instance）
    QRect dirty;
    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        dirty = unitedDirty(dirty, stampDab(tiles, last, radius, color, mode, hardness, clip));
        d += step;
    }
    // 末段不足一个 spacing 时补一个 dab，保证笔画末端不缺口
    if ((last - to).manhattanLength() > 0.5)
        dirty = unitedDirty(dirty, stampDab(tiles, to, radius, color, mode, hardness, clip));
    return dirty;
}

QRect PaintEngine::cloneStampDab(TileBuffer &tiles,
                                 const QPointF &center,
                                 const QPointF &sourceCenter,
                                 qreal radius,
                                 const QImage &sample,
                                 qreal hardness,
                                 qreal opacity,
                                 PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::CloneStampDab, ctx, [&](BufferOp &base) {
        auto &op = static_cast<CloneStampDabOp &>(base);
        op.setCenter(center);
        op.setSourceCenter(sourceCenter);
        op.setRadius(radius);
        op.setSample(&sample);
        op.setHardness(hardness);
        op.setOpacity(opacity);
    });
}

QRect PaintEngine::cloneStrokeSegment(TileBuffer &tiles,
                                      const QPointF &from,
                                      const QPointF &to,
                                      const QPointF &sourceFrom,
                                      const QPointF &sourceTo,
                                      qreal radius,
                                      const QImage &sample,
                                      qreal hardness,
                                      qreal opacity,
                                      qreal spacing,
                                      PaintSelectionClip clip)
{
    const QPointF delta = to - from;
    const QPointF srcDelta = sourceTo - sourceFrom;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6)
        return cloneStampDab(tiles, to, sourceTo, radius, sample, hardness, opacity, clip);

    QRect dirty;
    qreal d = 0.0;
    QPointF last = from;
    QPointF lastSrc = sourceFrom;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        lastSrc = sourceFrom + srcDelta * t;
        dirty = unitedDirty(dirty,
                            cloneStampDab(tiles, last, lastSrc, radius, sample,
                                          hardness, opacity, clip));
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5)
        dirty = unitedDirty(dirty,
                            cloneStampDab(tiles, to, sourceTo, radius, sample,
                                          hardness, opacity, clip));
    return dirty;
}

QRect PaintEngine::focusDab(TileBuffer &tiles,
                            const QPointF &center,
                            qreal radius,
                            FocusMode mode,
                            qreal strength,
                            qreal hardness,
                            const QPointF &smudgeDelta,
                            PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::FocusDab, ctx, [&](BufferOp &base) {
        auto &op = static_cast<FocusDabOp &>(base);
        op.setCenter(center);
        op.setRadius(radius);
        op.setMode(mode);
        op.setStrength(strength);
        op.setHardness(hardness);
        op.setSmudgeDelta(smudgeDelta);
    });
}

QRect PaintEngine::focusStrokeSegment(TileBuffer &tiles,
                                      const QPointF &from,
                                      const QPointF &to,
                                      qreal radius,
                                      FocusMode mode,
                                      qreal strength,
                                      qreal hardness,
                                      qreal spacing,
                                      PaintSelectionClip clip)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    // 涂抹：采样偏移 = 从当前指向来处（拖拽上一位置的颜色）
    auto smudgeAt = [&](const QPointF &prev, const QPointF &cur) -> QPointF {
        if (mode != FocusMode::Smudge)
            return {};
        return prev - cur;
    };

    if (len < 1e-6)
        return focusDab(tiles, to, radius, mode, strength, hardness,
                        smudgeAt(from, to), clip);

    QRect dirty;
    qreal d = 0.0;
    QPointF last = from;
    QPointF prev = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        dirty = unitedDirty(dirty,
                            focusDab(tiles, last, radius, mode, strength, hardness,
                                     smudgeAt(prev, last), clip));
        prev = last;
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5)
        dirty = unitedDirty(dirty,
                            focusDab(tiles, to, radius, mode, strength, hardness,
                                     smudgeAt(last, to), clip));
    return dirty;
}

QRect PaintEngine::toneDab(TileBuffer &tiles,
                           const QPointF &center,
                           qreal radius,
                           ToneMode mode,
                           qreal strength,
                           qreal hardness,
                           PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::ToneDab, ctx, [&](BufferOp &base) {
        auto &op = static_cast<ToneDabOp &>(base);
        op.setCenter(center);
        op.setRadius(radius);
        op.setMode(mode);
        op.setStrength(strength);
        op.setHardness(hardness);
    });
}

QRect PaintEngine::toneStrokeSegment(TileBuffer &tiles,
                                     const QPointF &from,
                                     const QPointF &to,
                                     qreal radius,
                                     ToneMode mode,
                                     qreal strength,
                                     qreal hardness,
                                     qreal spacing,
                                     PaintSelectionClip clip)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6)
        return toneDab(tiles, to, radius, mode, strength, hardness, clip);

    QRect dirty;
    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        dirty = unitedDirty(dirty,
                            toneDab(tiles, last, radius, mode, strength, hardness, clip));
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5)
        dirty = unitedDirty(dirty,
                            toneDab(tiles, to, radius, mode, strength, hardness, clip));
    return dirty;
}

QRect PaintEngine::fillShape(TileBuffer &tiles,
                             ShapeKind kind,
                             const QRectF &rect,
                             const QColor &color,
                             bool fill,
                             bool stroke,
                             qreal strokeWidth,
                             qreal cornerRadius,
                             bool antialias,
                             PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::ShapeFill, ctx, [&](BufferOp &base) {
        auto &op = static_cast<ShapeFillOp &>(base);
        op.setKind(kind);
        op.setRect(rect);
        op.setColor(color);
        op.setFill(fill);
        op.setStroke(stroke);
        op.setStrokeWidth(strokeWidth);
        op.setCornerRadius(cornerRadius);
        op.setAntialias(antialias);
    });
}

QRect PaintEngine::freeTransform(TileBuffer &tiles,
                                 const QRect &sourceRect,
                                 const QImage &sourcePixels,
                                 const QPointF destCorners[4],
                                 bool clearSource,
                                 PaintSelectionClip clip,
                                 TransformInterpolation interpolation)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::FreeTransform, ctx, [&](BufferOp &base) {
        auto &op = static_cast<FreeTransformOp &>(base);
        op.setSource(sourceRect, sourcePixels);
        op.setDestQuad(destCorners);
        op.setClearSource(clearSource);
        op.setInterpolation(interpolation);
    });
}

QRect PaintEngine::floodFill(TileBuffer &tiles,
                             const QPoint &seed,
                             const QColor &fillColor,
                             int tolerance,
                             bool contiguous,
                             PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::FloodFill, ctx, [&](BufferOp &base) {
        auto &op = static_cast<FloodFillOp &>(base);
        op.setSeed(seed);
        op.setFillColor(fillColor);
        op.setTolerance(tolerance);
        op.setContiguous(contiguous);
    });
}

QRect PaintEngine::fillGradient(TileBuffer &tiles,
                                 const QPointF &start,
                                 const QPointF &end,
                                 const QColor &foreground,
                                 const QColor &background,
                                 GradientType type,
                                 qreal opacity,
                                 int offsetPercent,
                                 bool reverse,
                                 bool dither,
                                 PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::Gradient, ctx, [&](BufferOp &base) {
        auto &op = static_cast<GradientOp &>(base);
        op.setStart(start);
        op.setEnd(end);
        op.setForeground(foreground);
        op.setBackground(background);
        op.setType(type);
        op.setOpacity(opacity);
        op.setOffsetPercent(offsetPercent);
        op.setReverse(reverse);
        op.setDither(dither);
    });
}

QRect PaintEngine::solidFill(TileBuffer &tiles,
                             const QColor &color,
                             PaintSelectionClip clip)
{
    OpContext ctx = OpContext::fromTiles(tiles, clip);
    return OpRunner::run(OpName::SolidFill, ctx, [&](BufferOp &base) {
        static_cast<SolidFillOp &>(base).setColor(color);
    });
}

QRect PaintEngine::selectPolygon(Selection &selection,
                                 const QPolygonF &points,
                                 ChannelOp op)
{
    OpContext ctx = OpContext::fromSelection(selection);
    return OpRunner::run(OpName::SelectPolygon, ctx, [&](BufferOp &base) {
        auto &selOp = static_cast<SelectPolygonOp &>(base);
        selOp.setPoints(points);
        selOp.setChannelOp(op);
    });
}

QRect PaintEngine::selectFlood(Selection &selection,
                               const QImage &sample,
                               const QPoint &seed,
                               int tolerance,
                               bool contiguous,
                               ChannelOp op)
{
    OpContext ctx = OpContext::fromSelection(selection);
    return OpRunner::run(OpName::SelectFlood, ctx, [&](BufferOp &base) {
        auto &selOp = static_cast<SelectFloodOp &>(base);
        selOp.setSampleImage(sample);
        selOp.setSeed(seed);
        selOp.setTolerance(tolerance);
        selOp.setContiguous(contiguous);
        selOp.setChannelOp(op);
    });
}

} // namespace Ps
