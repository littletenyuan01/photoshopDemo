/**
 * paintengine.cpp — paintengine.h 实现（engine 层）。
 *
 * dab / 洪泛 / 渐变 / 实色填充 / 多边形选区均走 OpRunner::run；
 * strokeSegment 沿路径插值多 dab。
 */
#include "paintengine.h"

#include "domain/tilebuffer.h"
#include "engine/op/floodfillop.h"
#include "engine/op/gradientop.h"
#include "engine/op/opcontext.h"
#include "engine/op/opname.h"
#include "engine/op/oprunner.h"
#include "engine/op/selectpolygonop.h"
#include "engine/op/solidfillop.h"
#include "engine/op/stampdabop.h"

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

} // namespace Ps
