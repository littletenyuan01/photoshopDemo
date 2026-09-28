#include "paintengine.h"

#include "domain/tilebuffer.h"

#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

namespace Ps {

namespace {

QRect dabBounds(const QPointF &center, qreal radius)
{
    const int rCeil = qCeil(radius) + 1;
    return QRect(qFloor(center.x()) - rCeil,
                 qFloor(center.y()) - rCeil,
                 rCeil * 2 + 1,
                 rCeil * 2 + 1);
}

void stampDabOnImage(QImage &target,
                     const QPointF &center,
                     qreal radius,
                     const QColor &color,
                     PaintEngine::Mode mode,
                     qreal hardness)
{
    if (target.isNull() || radius <= 0.0)
        return;

    hardness = qBound(0.0, hardness, 1.0);

    const QRect clip = dabBounds(center, radius).intersected(target.rect());
    if (clip.isEmpty())
        return;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clip);

    if (mode == PaintEngine::Mode::Erase)
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    else
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QColor core = (mode == PaintEngine::Mode::Erase) ? QColor(255, 255, 255) : color;
    QColor edge = core;
    core.setAlpha(255);
    edge.setAlpha(0);

    QRadialGradient grad(center, radius);
    const qreal stop = qBound(0.05, hardness, 0.98);
    grad.setColorAt(0.0, core);
    grad.setColorAt(stop, core);
    grad.setColorAt(1.0, edge);

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawEllipse(center, radius, radius);
}

template <typename Target>
QPointF strokeSegmentImpl(Target &target,
                          const QPointF &from,
                          const QPointF &to,
                          qreal radius,
                          const QColor &color,
                          PaintEngine::Mode mode,
                          qreal hardness,
                          qreal spacing)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness);
        return to;
    }

    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        PaintEngine::stampDab(target, last, radius, color, mode, hardness);
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5) {
        PaintEngine::stampDab(target, to, radius, color, mode, hardness);
        last = to;
    }
    return last;
}

} // namespace

void PaintEngine::stampDab(QImage &target,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness)
{
    stampDabOnImage(target, center, radius, color, mode, hardness);
}

void PaintEngine::stampDab(TileBuffer &tiles,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness)
{
    if (tiles.width() <= 0 || tiles.height() <= 0 || radius <= 0.0)
        return;

    const QRect dabRect = dabBounds(center, radius);
    tiles.forEachTileInRect(dabRect, true, [&](int, int, QImage &tile, const QRect &bounds) {
        const QPointF local(center.x() - bounds.x(), center.y() - bounds.y());
        stampDabOnImage(tile, local, radius, color, mode, hardness);
    });
}

QPointF PaintEngine::strokeSegment(QImage &target,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing)
{
    return strokeSegmentImpl(target, from, to, radius, color, mode, hardness, spacing);
}

QPointF PaintEngine::strokeSegment(TileBuffer &tiles,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing)
{
    return strokeSegmentImpl(tiles, from, to, radius, color, mode, hardness, spacing);
}

} // namespace Ps
