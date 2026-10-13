/**
 * pathstrokeop.cpp — PathStrokeOp 实现。
 */
#include "pathstrokeop.h"

#include "domain/tilebuffer.h"
#include "engine/bezier.h"

#include <QLineF>
#include <QtMath>

namespace Ps {

namespace {

QRect unitedDirty(const QRect &a, const QRect &b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    return a.united(b);
}

/** 把子路径折成折线（含闭合回首点）。 */
QVector<QPointF> flattenSub(const SubPath &sp, qreal tol)
{
    QVector<QPointF> poly;
    const int sc = sp.segmentCount();
    for (int seg = 0; seg < sc; ++seg) {
        const int i0 = seg;
        const int i1 = (seg + 1 < sp.anchors.size()) ? (seg + 1) : 0;
        const Bezier::Seg s = Bezier::fromAnchors(sp.anchors[i0], sp.anchors[i1]);
        const QVector<QPointF> piece = Bezier::flatten(s, tol);
        if (poly.isEmpty()) {
            poly = piece;
        } else {
            if (!piece.isEmpty() && poly.last() == piece.first())
                poly.removeLast();
            poly += piece;
        }
    }
    return poly;
}

} // namespace

QVector<QPointF> PathStrokeOp::dabCenters(const Path &path, qreal halfWidth, qreal spacing)
{
    QVector<QPointF> centers;
    const qreal radius = qMax(0.5, halfWidth);
    const qreal step = qMax(0.5, spacing * 2.0 * radius);

    for (const SubPath &sp : path.subPaths) {
        if (sp.isEmpty())
            continue;
        const QVector<QPointF> poly = flattenSub(sp, 0.25);
        if (poly.size() < 2)
            continue;

        centers.append(poly.first());
        qreal carry = 0.0; // 距上一 dab 已走过的弧长
        for (int i = 1; i < poly.size(); ++i) {
            QPointF a = poly[i - 1];
            const QPointF b = poly[i];
            qreal segLen = QLineF(a, b).length();
            if (segLen < 1e-9)
                continue;
            while (carry + segLen >= step) {
                const qreal remain = step - carry;
                const qreal u = remain / segLen;
                const QPointF p = a + u * (b - a);
                centers.append(p);
                a = p;
                segLen = QLineF(a, b).length();
                carry = 0.0;
            }
            carry += segLen;
        }
        // 闭合或末点：若末点距最后 dab 够远则再盖一颗，避免尾端空缺
        if (!centers.isEmpty() && QLineF(centers.last(), poly.last()).length() > step * 0.5)
            centers.append(poly.last());
    }
    return centers;
}

QRect PathStrokeOp::stroke(const Path &path, TileBuffer &tiles, const QColor &color,
                           qreal halfWidth, qreal spacing, PaintEngine::Mode mode,
                           qreal hardness, PaintSelectionClip clip)
{
    const QVector<QPointF> centers = dabCenters(path, halfWidth, spacing);
    QRect dirty;
    const qreal radius = qMax(0.5, halfWidth);
    for (const QPointF &c : centers) {
        dirty = unitedDirty(dirty,
                            PaintEngine::stampDab(tiles, c, radius, color, mode, hardness, clip));
    }
    return dirty;
}

} // namespace Ps
