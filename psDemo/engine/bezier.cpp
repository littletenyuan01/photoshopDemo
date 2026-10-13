/**
 * bezier.cpp — 三次贝塞尔纯函数实现。
 */
#include "bezier.h"

#include "domain/path.h"

#include <QLineF>
#include <QtMath>

namespace Ps {
namespace Bezier {

Seg fromAnchors(const PathAnchor &a, const PathAnchor &b)
{
    Seg s;
    s.p0 = a.point;
    s.p1 = a.outHandle;
    s.p2 = b.inHandle;
    s.p3 = b.point;
    return s;
}

QPointF pointAt(const Seg &s, qreal t)
{
    const qreal u = 1.0 - t;
    const qreal uu = u * u;
    const qreal tt = t * t;
    // B(t) = u³p0 + 3u²t p1 + 3u t² p2 + t³ p3
    return uu * u * s.p0 + 3.0 * uu * t * s.p1 + 3.0 * u * tt * s.p2 + tt * t * s.p3;
}

QPointF tangentAt(const Seg &s, qreal t)
{
    const qreal u = 1.0 - t;
    // B'(t) = 3u²(p1-p0) + 6u t(p2-p1) + 3t²(p3-p2)
    return 3.0 * u * u * (s.p1 - s.p0) + 6.0 * u * t * (s.p2 - s.p1)
        + 3.0 * t * t * (s.p3 - s.p2);
}

QPair<Seg, Seg> splitAt(const Seg &s, qreal t)
{
    // 标准 de Casteljau（一次分裂即可；t 不必是 0.5）
    const QPointF p01 = s.p0 + t * (s.p1 - s.p0);
    const QPointF p12 = s.p1 + t * (s.p2 - s.p1);
    const QPointF p23 = s.p2 + t * (s.p3 - s.p2);
    const QPointF p012 = p01 + t * (p12 - p01);
    const QPointF p123 = p12 + t * (p23 - p12);
    const QPointF p = p012 + t * (p123 - p012);
    Seg left{s.p0, p01, p012, p};
    Seg right{p, p123, p23, s.p3};
    return qMakePair(left, right);
}

QRectF bounds(const Seg &s)
{
    qreal minX = s.p0.x(), maxX = s.p0.x();
    qreal minY = s.p0.y(), maxY = s.p0.y();
    const QPointF pts[4] = {s.p0, s.p1, s.p2, s.p3};
    for (const QPointF &p : pts) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

namespace {

qreal distPointToSeg(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 < 1e-18)
        return QLineF(p, a).length();
    qreal t = QPointF::dotProduct(p - a, ab) / ab2;
    t = qBound(0.0, t, 1.0);
    const QPointF q = a + t * ab;
    return QLineF(p, q).length();
}

/** 控制点到弦的最大偏离；小于 tol 即可停止细分。 */
qreal flatness(const Seg &s)
{
    return qMax(distPointToSeg(s.p1, s.p0, s.p3), distPointToSeg(s.p2, s.p0, s.p3));
}

void flattenRec(const Seg &s, qreal tol, QVector<QPointF> *out, int *steps)
{
    if (flatness(s) <= tol) {
        if (out->isEmpty() || out->last() != s.p0)
            out->append(s.p0);
        out->append(s.p3);
        if (steps)
            ++(*steps);
        return;
    }
    const auto parts = splitAt(s, 0.5);
    flattenRec(parts.first, tol, out, steps);
    // 右段起点与左段终点重合，flattenRec 会因 last==p0 跳过重复
    if (!out->isEmpty())
        out->removeLast();
    flattenRec(parts.second, tol, out, steps);
}

} // namespace

QVector<QPointF> flatten(const Seg &s, qreal tol, int *stepsOut)
{
    QVector<QPointF> out;
    int steps = 0;
    const qreal t = qMax(tol, 1e-6);
    flattenRec(s, t, &out, &steps);
    if (stepsOut)
        *stepsOut = steps;
    return out;
}

qreal length(const Seg &s, int steps)
{
    const int n = qMax(1, steps);
    qreal len = 0.0;
    QPointF prev = pointAt(s, 0.0);
    for (int i = 1; i <= n; ++i) {
        const QPointF cur = pointAt(s, qreal(i) / n);
        len += QLineF(prev, cur).length();
        prev = cur;
    }
    return len;
}

QPointF pointAtLength(const Seg &s, qreal len, int steps)
{
    const int n = qMax(1, steps);
    if (len <= 0.0)
        return s.p0;
    qreal acc = 0.0;
    QPointF prev = pointAt(s, 0.0);
    for (int i = 1; i <= n; ++i) {
        const qreal t = qreal(i) / n;
        const QPointF cur = pointAt(s, t);
        const qreal segLen = QLineF(prev, cur).length();
        if (acc + segLen >= len) {
            const qreal remain = len - acc;
            const qreal u = (segLen > 1e-12) ? (remain / segLen) : 0.0;
            return prev + u * (cur - prev);
        }
        acc += segLen;
        prev = cur;
    }
    return s.p3;
}

} // namespace Bezier
} // namespace Ps
