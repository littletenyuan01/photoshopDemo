/**
 * t_pathgeom.cpp — 路径几何：贝塞尔分裂/扁平化/命中/自动曲线控制柄（pen-tool-plan P0）。
 */
#include "domain/path.h"
#include "engine/bezier.h"
#include "testutil.h"

#include <QPainterPath>
#include <QtMath>

#include <cmath>

using namespace Ps;
using PsTests::Reporter;

namespace {

PathAnchor makeAnchor(const QPointF &p, const QPointF &inH, const QPointF &outH)
{
    PathAnchor a;
    a.point = p;
    a.inHandle = inH;
    a.outHandle = outH;
    return a;
}

Path makeOpenCubic()
{
    Path path;
    SubPath sp;
    sp.closed = false;
    sp.anchors.append(makeAnchor({0, 0}, {0, 0}, {40, 80}));
    sp.anchors.append(makeAnchor({200, 0}, {160, -60}, {200, 0}));
    path.subPaths.append(sp);
    return path;
}

qreal maxSampleDiff(const Bezier::Seg &orig, const Bezier::Seg &part, qreal t0, qreal t1,
                    int samples = 40)
{
    qreal worst = 0.0;
    for (int i = 0; i <= samples; ++i) {
        const qreal u = qreal(i) / samples;
        const qreal tOrig = t0 + u * (t1 - t0);
        const QPointF a = Bezier::pointAt(orig, tOrig);
        const QPointF b = Bezier::pointAt(part, u);
        worst = qMax(worst, QLineF(a, b).length());
    }
    return worst;
}

} // namespace

int tPathGeom()
{
    Reporter r;

    // 1–2) splitAt 形状不变
    {
        const Path path = makeOpenCubic();
        const Bezier::Seg orig =
            Bezier::fromAnchors(path.subPaths[0].anchors[0], path.subPaths[0].anchors[1]);
        for (qreal t : {0.1, 0.25, 0.5, 0.75, 0.9}) {
            const auto parts = Bezier::splitAt(orig, t);
            const qreal dL = maxSampleDiff(orig, parts.first, 0.0, t);
            const qreal dR = maxSampleDiff(orig, parts.second, t, 1.0);
            r.check(dL < 1e-9 && dR < 1e-9,
                    QStringLiteral("splitAt(t=%1) 分段采样误差 < 1e-9（L=%2 R=%3）")
                        .arg(t, 0, 'f', 2)
                        .arg(dL, 0, 'g', 3)
                        .arg(dR, 0, 'g', 3));
            r.check(parts.first.p3 == parts.second.p0,
                    QStringLiteral("splitAt(t=%1) 接合点重合").arg(t, 0, 'f', 2));
        }
    }

    // 3) insertAnchor 后形状不变（参数对齐采样，不依赖 flatten 近似误差）
    {
        Path path = makeOpenCubic();
        const Bezier::Seg before =
            Bezier::fromAnchors(path.subPaths[0].anchors[0], path.subPaths[0].anchors[1]);
        const qreal tIns = 0.4;
        r.check(path.insertAnchor(0, 0, tIns), QStringLiteral("insertAnchor 成功"));
        r.check(path.subPaths[0].anchors.size() == 3, QStringLiteral("插入后锚点数=3"));

        const Bezier::Seg left =
            Bezier::fromAnchors(path.subPaths[0].anchors[0], path.subPaths[0].anchors[1]);
        const Bezier::Seg right =
            Bezier::fromAnchors(path.subPaths[0].anchors[1], path.subPaths[0].anchors[2]);
        qreal worst = 0.0;
        for (int i = 0; i <= 80; ++i) {
            const qreal t = qreal(i) / 80;
            const QPointF p0 = Bezier::pointAt(before, t);
            QPointF p1;
            if (t <= tIns) {
                const qreal u = (tIns > 1e-12) ? (t / tIns) : 0.0;
                p1 = Bezier::pointAt(left, u);
            } else {
                const qreal u = ((1.0 - tIns) > 1e-12) ? ((t - tIns) / (1.0 - tIns)) : 1.0;
                p1 = Bezier::pointAt(right, u);
            }
            worst = qMax(worst, QLineF(p0, p1).length());
        }
        // 另用紧 tol flatten 交叉核对（计划口径）
        const QVector<QPointF> flat0 = Bezier::flatten(before, 1e-7);
        QVector<QPointF> flat1 = Bezier::flatten(left, 1e-7);
        {
            const QVector<QPointF> piece = Bezier::flatten(right, 1e-7);
            if (!flat1.isEmpty() && !piece.isEmpty() && flat1.last() == piece.first())
                flat1.removeLast();
            flat1 += piece;
        }
        qreal flatWorst = 0.0;
        for (const QPointF &p : flat0) {
            qreal best = 1e9;
            for (int k = 0; k + 1 < flat1.size(); ++k) {
                const QPointF ab = flat1[k + 1] - flat1[k];
                const qreal ab2 = QPointF::dotProduct(ab, ab);
                qreal u = ab2 > 1e-18 ? QPointF::dotProduct(p - flat1[k], ab) / ab2 : 0.0;
                u = qBound(0.0, u, 1.0);
                best = qMin(best, QLineF(p, flat1[k] + u * ab).length());
            }
            flatWorst = qMax(flatWorst, best);
        }
        r.check(worst < 1e-9 && flatWorst < 1e-6,
                QStringLiteral("insertAnchor 后形状不变（采样 %1 / flatten %2）")
                    .arg(worst, 0, 'g', 3)
                    .arg(flatWorst, 0, 'g', 3));
    }

    // 4) deleteAnchor 两端锚点坐标不变
    {
        Path path = makeOpenCubic();
        path.insertAnchor(0, 0, 0.5);
        const QPointF end0 = path.subPaths[0].anchors.first().point;
        const QPointF end1 = path.subPaths[0].anchors.last().point;
        r.check(path.deleteAnchor(0, 1), QStringLiteral("deleteAnchor 中间点"));
        r.check(path.subPaths[0].anchors.first().point == end0
                    && path.subPaths[0].anchors.last().point == end1,
                QStringLiteral("删除后两端锚点坐标不变"));
    }

    // 5) bounds 包住 flatten
    {
        const Path path = makeOpenCubic();
        const Bezier::Seg s =
            Bezier::fromAnchors(path.subPaths[0].anchors[0], path.subPaths[0].anchors[1]);
        const QRectF b = Bezier::bounds(s);
        const QVector<QPointF> pts = Bezier::flatten(s, 0.1);
        bool ok = true;
        for (const QPointF &p : pts) {
            if (!b.contains(p) && !b.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(p))
                ok = false;
        }
        r.check(ok, QStringLiteral("bounds 包住 flatten 所有点"));
    }

    // 6) moveAnchor 控制柄相对偏移不变
    {
        Path path = makeOpenCubic();
        PathAnchor &a = path.subPaths[0].anchors[0];
        const QPointF inOff = a.inHandle - a.point;
        const QPointF outOff = a.outHandle - a.point;
        path.moveAnchor(0, 0, QPointF(30, 40));
        r.check(a.inHandle - a.point == inOff && a.outHandle - a.point == outOff,
                QStringLiteral("moveAnchor 后控制柄相对偏移不变"));
    }

    // 7) hitTestAnchor 边界
    {
        Path path = makeOpenCubic();
        const QPointF p = path.subPaths[0].anchors[0].point;
        const qreal tol = 5.0;
        r.check(path.hitTestAnchor(p + QPointF(tol, 0), tol).index == 0,
                QStringLiteral("tol 内命中锚点"));
        r.check(path.hitTestAnchor(p + QPointF(tol * 1.1, 0), tol).sub < 0,
                QStringLiteral("tol*1.1 外不命中"));
    }

    // 8) hitTestHandle 只命中 selected
    {
        Path path = makeOpenCubic();
        path.subPaths[0].anchors[0].selected = true;
        path.subPaths[0].anchors[1].selected = false;
        const QPointF h = path.subPaths[0].anchors[1].inHandle;
        r.check(path.hitTestHandle(h, 5.0, true).sub < 0,
                QStringLiteral("未选中锚点的控制柄不可命中"));
        path.subPaths[0].anchors[0].selected = true;
        const QPointF h0 = path.subPaths[0].anchors[0].outHandle;
        r.check(path.hitTestHandle(h0, 5.0, true).index == 0,
                QStringLiteral("选中锚点的控制柄可命中"));
    }

    // 9) nearestOnPath 的 t 与手工采样一致
    {
        Path path = makeOpenCubic();
        const Bezier::Seg s =
            Bezier::fromAnchors(path.subPaths[0].anchors[0], path.subPaths[0].anchors[1]);
        const qreal tTarget = 0.37;
        const QPointF q = Bezier::pointAt(s, tTarget);
        const Path::SegHit hit = path.nearestOnPath(q, 50.0);
        r.check(hit.sub == 0 && hit.seg == 0 && qAbs(hit.t - tTarget) < 0.04,
                QStringLiteral("nearestOnPath t≈%1（得 %2）").arg(tTarget).arg(hit.t));
    }

    // 10) 三点共线 → 控制柄在连线上
    {
        Path path;
        SubPath sp;
        sp.closed = false;
        for (int i = 0; i < 3; ++i) {
            PathAnchor a;
            a.point = QPointF(i * 50.0, 0);
            a.inHandle = a.outHandle = a.point;
            sp.anchors.append(a);
        }
        path.subPaths.append(sp);
        path.recomputeAutoHandles(0, 1.0);
        bool onLine = true;
        for (const PathAnchor &a : path.subPaths[0].anchors) {
            if (qAbs(a.inHandle.y()) > 1e-9 || qAbs(a.outHandle.y()) > 1e-9)
                onLine = false;
        }
        r.check(onLine, QStringLiteral("共线三点：自动控制柄落在连线上"));
    }

    // 11) explicit 不被覆盖
    {
        Path path;
        SubPath sp;
        for (int i = 0; i < 3; ++i) {
            PathAnchor a;
            a.point = QPointF(i * 40.0, i * 10.0);
            a.inHandle = a.outHandle = a.point;
            sp.anchors.append(a);
        }
        path.subPaths.append(sp);
        path.subPaths[0].anchors[1].outHandle = QPointF(999, 999);
        path.subPaths[0].anchors[1].outExplicit = true;
        path.recomputeAutoHandles(0, 1.0);
        r.check(path.subPaths[0].anchors[1].outHandle == QPointF(999, 999),
                QStringLiteral("recomputeAutoHandles 不覆盖 explicit 侧"));
        r.check(path.subPaths[0].anchors[1].inHandle != QPointF(999, 999)
                    || !path.subPaths[0].anchors[1].inExplicit,
                QStringLiteral("非 explicit 的 in 侧仍可被重算"));
    }

    // 12) toPainterPath ↔ fromPainterPath
    {
        Path path = makeOpenCubic();
        path.subPaths[0].anchors[0].outExplicit = true;
        path.subPaths[0].anchors[1].inExplicit = true;
        const QPainterPath pp = path.toPainterPath();
        const Path back = Path::fromPainterPath(pp);
        r.check(back.subPaths.size() == 1 && back.subPaths[0].anchors.size() == 2,
                QStringLiteral("往返锚点数=2"));
        if (back.subPaths.size() == 1 && back.subPaths[0].anchors.size() == 2) {
            const PathAnchor &a0 = path.subPaths[0].anchors[0];
            const PathAnchor &b0 = back.subPaths[0].anchors[0];
            const PathAnchor &a1 = path.subPaths[0].anchors[1];
            const PathAnchor &b1 = back.subPaths[0].anchors[1];
            const qreal e =
                QLineF(a0.point, b0.point).length() + QLineF(a0.outHandle, b0.outHandle).length()
                + QLineF(a1.inHandle, b1.inHandle).length() + QLineF(a1.point, b1.point).length();
            r.check(e < 1e-9, QStringLiteral("往返控制柄误差 < 1e-9（%1）").arg(e, 0, 'g', 3));
        }
    }

    // 13) segmentCount
    {
        Path open = makeOpenCubic();
        r.check(open.subPaths[0].segmentCount() == open.subPaths[0].anchors.size() - 1,
                QStringLiteral("开路径 segmentCount == 锚点数-1"));
        Path closed;
        SubPath sp;
        sp.closed = true;
        for (int i = 0; i < 4; ++i) {
            PathAnchor a;
            a.point = QPointF(i, 0);
            a.inHandle = a.outHandle = a.point;
            sp.anchors.append(a);
        }
        closed.subPaths.append(sp);
        r.check(closed.subPaths[0].segmentCount() == closed.subPaths[0].anchors.size(),
                QStringLiteral("闭路径 segmentCount == 锚点数"));
    }

    return r.fails;
}
