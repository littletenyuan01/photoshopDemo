#include "paintengine.h"

#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

namespace Ps {

void PaintEngine::stampDab(QImage &target,
                           const QPointF &center,
                           qreal radius,
                           const QColor &color,
                           Mode mode,
                           qreal hardness)
{
    if (target.isNull() || radius <= 0.0)
        return;

    hardness = qBound(0.0, hardness, 1.0);

    // dab 外接矩形，略扩 1px 避免抗锯齿裁切
    const int rCeil = qCeil(radius) + 1;
    const QRect dabRect(qFloor(center.x()) - rCeil,
                        qFloor(center.y()) - rCeil,
                        rCeil * 2 + 1,
                        rCeil * 2 + 1);
    const QRect clip = dabRect.intersected(target.rect());
    if (clip.isEmpty())
        return;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(clip);

    if (mode == Mode::Erase)
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    else
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // 径向渐变：硬核内 alpha=1，外缘按 hardness 衰减到 0
    // DestinationOut 时用白色+alpha 表示「擦除强度」
    QColor core = (mode == Mode::Erase) ? QColor(255, 255, 255) : color;
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

QPointF PaintEngine::strokeSegment(QImage &target,
                                   const QPointF &from,
                                   const QPointF &to,
                                   qreal radius,
                                   const QColor &color,
                                   Mode mode,
                                   qreal hardness,
                                   qreal spacing)
{
    const QPointF delta = to - from;
    const qreal len = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
    const qreal step = qMax(0.5, radius * 2.0 * qBound(0.05, spacing, 1.0));

    if (len < 1e-6) {
        stampDab(target, to, radius, color, mode, hardness);
        return to;
    }

    // 沿线段等距盖 dab；终点再补一颗，避免快速拖动断笔
    qreal d = 0.0;
    QPointF last = from;
    while (d <= len) {
        const qreal t = d / len;
        last = from + delta * t;
        stampDab(target, last, radius, color, mode, hardness);
        d += step;
    }
    if ((last - to).manhattanLength() > 0.5) {
        stampDab(target, to, radius, color, mode, hardness);
        last = to;
    }
    return last;
}

} // namespace Ps
