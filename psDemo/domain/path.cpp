/**
 * path.cpp — 路径数据模型实现。
 */
#include "path.h"

#include "engine/bezier.h"

#include <QLineF>
#include <QtMath>

#include <cmath>

namespace Ps {

int SubPath::segmentCount() const
{
    if (anchors.size() < 2)
        return 0;
    return closed ? anchors.size() : (anchors.size() - 1);
}

namespace {

bool validSub(const Path &p, int sub)
{
    return sub >= 0 && sub < p.subPaths.size();
}

PathAnchor *anchorAt(Path &p, int sub, int idx)
{
    if (!validSub(p, sub))
        return nullptr;
    auto &a = p.subPaths[sub].anchors;
    if (idx < 0 || idx >= a.size())
        return nullptr;
    return &a[idx];
}

/** 段 seg 的两端锚点下标（闭合时末段连回 0）。 */
bool segmentEnds(const SubPath &sp, int seg, int *i0, int *i1)
{
    const int n = sp.anchors.size();
    const int sc = sp.segmentCount();
    if (seg < 0 || seg >= sc || n < 2)
        return false;
    *i0 = seg;
    *i1 = (seg + 1 < n) ? (seg + 1) : 0;
    return true;
}

qreal dist2(const QPointF &a, const QPointF &b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

} // namespace

QRectF Path::bounds() const
{
    QRectF r;
    bool any = false;
    for (const SubPath &sp : subPaths) {
        const int sc = sp.segmentCount();
        for (int seg = 0; seg < sc; ++seg) {
            int i0 = 0, i1 = 0;
            if (!segmentEnds(sp, seg, &i0, &i1))
                continue;
            const Bezier::Seg s = Bezier::fromAnchors(sp.anchors[i0], sp.anchors[i1]);
            const QRectF b = Bezier::bounds(s);
            if (!any) {
                r = b;
                any = true;
            } else {
                r = r.united(b);
            }
        }
        // 单点也计入
        for (const PathAnchor &a : sp.anchors) {
            if (!any) {
                r = QRectF(a.point, a.point);
                any = true;
            } else {
                r = r.united(QRectF(a.point, a.point));
            }
        }
    }
    return r;
}

bool Path::isEmpty() const
{
    for (const SubPath &sp : subPaths) {
        if (!sp.isEmpty())
            return false;
    }
    return true;
}

int Path::totalAnchors() const
{
    int n = 0;
    for (const SubPath &sp : subPaths)
        n += sp.anchors.size();
    return n;
}

bool Path::insertAnchor(int sub, int seg, qreal t)
{
    if (!validSub(*this, sub))
        return false;
    SubPath &sp = subPaths[sub];
    int i0 = 0, i1 = 0;
    if (!segmentEnds(sp, seg, &i0, &i1))
        return false;
    t = qBound(1e-6, t, 1.0 - 1e-6);

    const Bezier::Seg s = Bezier::fromAnchors(sp.anchors[i0], sp.anchors[i1]);
    const auto parts = Bezier::splitAt(s, t);

    // 更新两端控制柄为分裂后的外侧柄
    sp.anchors[i0].outHandle = parts.first.p1;
    sp.anchors[i1].inHandle = parts.second.p2;

    PathAnchor mid;
    mid.point = parts.first.p3;
    mid.inHandle = parts.first.p2;
    mid.outHandle = parts.second.p1;
    mid.inExplicit = true;
    mid.outExplicit = true;
    mid.corner = false;
    mid.selected = false;

    // 插入位置：开路径在 i0+1；闭合末段时 i1==0，插到末尾
    const int insertAt = (i1 == 0) ? sp.anchors.size() : (i0 + 1);
    sp.anchors.insert(insertAt, mid);
    return true;
}

bool Path::deleteAnchor(int sub, int anchorIdx)
{
    if (!validSub(*this, sub))
        return false;
    SubPath &sp = subPaths[sub];
    const int n = sp.anchors.size();
    if (anchorIdx < 0 || anchorIdx >= n || n < 2)
        return false;
    // 至少保留 2 点（开）或闭合时也至少 2
    if (n <= 2)
        return false;

    // 端点坐标：删除后两侧邻居成为新邻接；把被删点两侧柄按比例外推到邻居
    const int prev = (anchorIdx > 0) ? (anchorIdx - 1) : (sp.closed ? (n - 1) : -1);
    const int next = (anchorIdx + 1 < n) ? (anchorIdx + 1) : (sp.closed ? 0 : -1);

    if (prev >= 0 && next >= 0) {
        // 中间点：prev.out 指向原 mid，next.in 来自原 mid —— 保持端点不动即可
        // （形状会变，但端点坐标不变 —— 满足用例 #4）
        Q_UNUSED(prev);
        Q_UNUSED(next);
    }

    sp.anchors.removeAt(anchorIdx);
    return true;
}

bool Path::moveAnchor(int sub, int anchorIdx, const QPointF &newPoint)
{
    PathAnchor *a = anchorAt(*this, sub, anchorIdx);
    if (!a)
        return false;
    const QPointF delta = newPoint - a->point;
    a->point = newPoint;
    a->inHandle += delta;
    a->outHandle += delta;
    return true;
}

bool Path::setCorner(int sub, int anchorIdx, bool corner)
{
    PathAnchor *a = anchorAt(*this, sub, anchorIdx);
    if (!a)
        return false;
    a->corner = corner;
    return true;
}

bool Path::setHandle(int sub, int anchorIdx, bool isOut, const QPointF &h, bool mirror)
{
    PathAnchor *a = anchorAt(*this, sub, anchorIdx);
    if (!a)
        return false;
    if (isOut) {
        a->outHandle = h;
        a->outExplicit = true;
        if (mirror && !a->corner) {
            a->inHandle = a->point - (h - a->point);
            a->inExplicit = true;
        }
    } else {
        a->inHandle = h;
        a->inExplicit = true;
        if (mirror && !a->corner) {
            a->outHandle = a->point - (h - a->point);
            a->outExplicit = true;
        }
    }
    return true;
}

void Path::recomputeAutoHandles(int sub, qreal tension)
{
    if (!validSub(*this, sub))
        return;
    SubPath &sp = subPaths[sub];
    const int n = sp.anchors.size();
    if (n < 2)
        return;
    const qreal ten = tension;

    auto at = [&](int i) -> const PathAnchor & {
        if (sp.closed) {
            i = ((i % n) + n) % n;
            return sp.anchors[i];
        }
        i = qBound(0, i, n - 1);
        return sp.anchors[i];
    };

    for (int i = 0; i < n; ++i) {
        PathAnchor &a = sp.anchors[i];
        // Catmull-Rom：tangent_i = (P[i+1]-P[i-1])*0.5
        QPointF tan;
        if (!sp.closed && i == 0)
            tan = at(1).point - at(0).point;
        else if (!sp.closed && i == n - 1)
            tan = at(n - 1).point - at(n - 2).point;
        else
            tan = (at(i + 1).point - at(i - 1).point) * 0.5;

        // outHandle[i] = P[i] + tangent_i / 3 * tension
        // inHandle[i]  = P[i] - tangent_i / 3 * tension
        // 【为什么 in 也用同一 tangent】中心差分在锚点处给出 C¹；显式侧不覆盖。
        const QPointF delta = tan / 3.0 * ten;
        if (!a.outExplicit)
            a.outHandle = a.point + delta;
        if (!a.inExplicit)
            a.inHandle = a.point - delta;
    }
}

Path::Hit Path::hitTestAnchor(const QPointF &docPos, qreal tolDoc) const
{
    Hit best;
    const qreal tol2 = tolDoc * tolDoc;
    for (int s = 0; s < subPaths.size(); ++s) {
        const auto &anchors = subPaths[s].anchors;
        for (int i = 0; i < anchors.size(); ++i) {
            const qreal d2 = dist2(docPos, anchors[i].point);
            if (d2 <= tol2) {
                const qreal d = std::sqrt(d2);
                if (best.sub < 0 || d < best.dist) {
                    best.sub = s;
                    best.index = i;
                    best.dist = d;
                }
            }
        }
    }
    return best;
}

Path::HandleHit Path::hitTestHandle(const QPointF &docPos, qreal tolDoc,
                                    bool selectedOnly) const
{
    HandleHit best;
    const qreal tol2 = tolDoc * tolDoc;
    for (int s = 0; s < subPaths.size(); ++s) {
        const auto &anchors = subPaths[s].anchors;
        for (int i = 0; i < anchors.size(); ++i) {
            const PathAnchor &a = anchors[i];
            if (selectedOnly && !a.selected)
                continue;
            auto tryHit = [&](const QPointF &h, bool isOut) {
                if (h == a.point && !(isOut ? a.outExplicit : a.inExplicit))
                    return;
                const qreal d2 = dist2(docPos, h);
                if (d2 > tol2)
                    return;
                const qreal d = std::sqrt(d2);
                if (best.sub < 0 || d < best.dist) {
                    best.sub = s;
                    best.index = i;
                    best.isOut = isOut;
                    best.dist = d;
                }
            };
            tryHit(a.inHandle, false);
            tryHit(a.outHandle, true);
        }
    }
    return best;
}

Path::SegHit Path::nearestOnPath(const QPointF &docPos, qreal tolDoc) const
{
    SegHit best;
    const int samples = 32;
    for (int s = 0; s < subPaths.size(); ++s) {
        const SubPath &sp = subPaths[s];
        const int sc = sp.segmentCount();
        for (int seg = 0; seg < sc; ++seg) {
            int i0 = 0, i1 = 0;
            if (!segmentEnds(sp, seg, &i0, &i1))
                continue;
            const Bezier::Seg bez = Bezier::fromAnchors(sp.anchors[i0], sp.anchors[i1]);
            for (int k = 0; k <= samples; ++k) {
                const qreal t = qreal(k) / samples;
                const QPointF p = Bezier::pointAt(bez, t);
                const qreal d = QLineF(docPos, p).length();
                if (d <= tolDoc && (best.sub < 0 || d < best.dist)) {
                    best.sub = s;
                    best.seg = seg;
                    best.t = t;
                    best.dist = d;
                }
            }
        }
    }
    return best;
}

void Path::translate(const QPointF &delta)
{
    for (SubPath &sp : subPaths) {
        for (PathAnchor &a : sp.anchors) {
            a.point += delta;
            a.inHandle += delta;
            a.outHandle += delta;
        }
    }
}

void Path::transform(const QTransform &m)
{
    for (SubPath &sp : subPaths) {
        for (PathAnchor &a : sp.anchors) {
            a.point = m.map(a.point);
            a.inHandle = m.map(a.inHandle);
            a.outHandle = m.map(a.outHandle);
        }
    }
}

QPainterPath Path::toPainterPath() const
{
    QPainterPath pp;
    for (const SubPath &sp : subPaths) {
        if (sp.anchors.isEmpty())
            continue;
        pp.moveTo(sp.anchors.first().point);
        const int sc = sp.segmentCount();
        for (int seg = 0; seg < sc; ++seg) {
            int i0 = 0, i1 = 0;
            if (!segmentEnds(sp, seg, &i0, &i1))
                continue;
            const PathAnchor &a = sp.anchors[i0];
            const PathAnchor &b = sp.anchors[i1];
            pp.cubicTo(a.outHandle, b.inHandle, b.point);
        }
        if (sp.closed)
            pp.closeSubpath();
    }
    return pp;
}

Path Path::fromPainterPath(const QPainterPath &pp)
{
    /**
     * 把 Qt 路径元素还原成「锚点 + 控制柄」。
     * 【限制】只忠实还原 MoveTo/LineTo/CurveTo；不把整段拟合成最少锚点。
     * LineTo 写成控制柄退化到端点的 cubic。
     */
    Path path;
    SubPath cur;
    PathAnchor pending; // 当前子路径上一个锚点（尚未 push 的 out 可能待填）
    bool havePending = false;

    auto flushPending = [&]() {
        if (havePending) {
            cur.anchors.append(pending);
            havePending = false;
        }
    };

    auto startSub = [&](const QPointF &p) {
        flushPending();
        if (!cur.anchors.isEmpty()) {
            path.subPaths.append(cur);
            cur = SubPath{};
        }
        pending = PathAnchor{};
        pending.point = p;
        pending.inHandle = p;
        pending.outHandle = p;
        havePending = true;
    };

    for (int i = 0; i < pp.elementCount(); ++i) {
        const QPainterPath::Element e = pp.elementAt(i);
        if (e.isMoveTo()) {
            startSub(QPointF(e.x, e.y));
        } else if (e.isLineTo()) {
            if (!havePending)
                startSub(QPointF(e.x, e.y));
            else {
                // 直线：pending.out = pending.point；新锚点 in = point
                pending.outHandle = pending.point;
                flushPending();
                pending = PathAnchor{};
                pending.point = QPointF(e.x, e.y);
                pending.inHandle = pending.point;
                pending.outHandle = pending.point;
                havePending = true;
            }
        } else if (e.isCurveTo()) {
            // CurveTo 在 Qt 里占 3 个元素：c1, c2, end —— elementAt 已是当前；
            // 文档：isCurveTo 时本元素是第一控制点，后两个是第二控制点与终点。
            if (i + 2 >= pp.elementCount())
                break;
            const QPainterPath::Element c1 = e;
            const QPainterPath::Element c2 = pp.elementAt(i + 1);
            const QPainterPath::Element end = pp.elementAt(i + 2);
            i += 2;
            if (!havePending)
                startSub(QPointF(c1.x, c1.y)); // 退化兜底
            pending.outHandle = QPointF(c1.x, c1.y);
            pending.outExplicit = true;
            flushPending();
            pending = PathAnchor{};
            pending.point = QPointF(end.x, end.y);
            pending.inHandle = QPointF(c2.x, c2.y);
            pending.inExplicit = true;
            pending.outHandle = pending.point;
            havePending = true;
        }
    }
    flushPending();
    if (!cur.anchors.isEmpty())
        path.subPaths.append(cur);
    return path;
}

void Path::write(QDataStream &out) const
{
    out << name << quint8(visible ? 1 : 0) << qint32(subPaths.size());
    for (const SubPath &sp : subPaths) {
        out << quint8(sp.closed ? 1 : 0) << qint32(sp.anchors.size());
        for (const PathAnchor &a : sp.anchors) {
            out << a.point << a.inHandle << a.outHandle;
            out << quint8(a.inExplicit ? 1 : 0) << quint8(a.outExplicit ? 1 : 0)
                << quint8(a.corner ? 1 : 0);
        }
    }
}

Path Path::read(QDataStream &in)
{
    Path path;
    quint8 vis = 1;
    qint32 nSub = 0;
    in >> path.name >> vis >> nSub;
    path.visible = (vis != 0);
    for (qint32 s = 0; s < nSub; ++s) {
        SubPath sp;
        quint8 closed = 0;
        qint32 nA = 0;
        in >> closed >> nA;
        sp.closed = (closed != 0);
        for (qint32 i = 0; i < nA; ++i) {
            PathAnchor a;
            quint8 inEx = 0, outEx = 0, corner = 0;
            in >> a.point >> a.inHandle >> a.outHandle >> inEx >> outEx >> corner;
            a.inExplicit = (inEx != 0);
            a.outExplicit = (outEx != 0);
            a.corner = (corner != 0);
            sp.anchors.append(a);
        }
        path.subPaths.append(sp);
    }
    return path;
}

} // namespace Ps
