/**
 * selectiontopath.cpp — Moore 轮廓 + Douglas–Peucker 实现。
 */
#include "selectiontopath.h"

#include <QBitArray>
#include <QLineF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace Ps {

namespace {

bool isFg(const QImage &bin, int x, int y)
{
    if (x < 0 || y < 0 || x >= bin.width() || y >= bin.height())
        return false;
    return bin.constScanLine(y)[x] != 0;
}

/** 二值化：>127 → 255。 */
QImage binarize(const QImage &gray)
{
    QImage bin(gray.size(), QImage::Format_Grayscale8);
    bin.fill(0);
    if (gray.isNull())
        return bin;
    const QImage src = (gray.format() == QImage::Format_Grayscale8)
                           ? gray
                           : gray.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const uchar *s = src.constScanLine(y);
        uchar *d = bin.scanLine(y);
        for (int x = 0; x < src.width(); ++x)
            d[x] = (s[x] > 127) ? 255 : 0;
    }
    return bin;
}

/** 8 邻域：从东起顺时针（Moore）。 */
const int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};

/**
 * 从边界点起步沿轮廓走一圈。
 * @param startDir 进入起点时「背向」的邻域下标，使第一次搜索从左邻起。
 */
QVector<QPoint> traceMoore(const QImage &bin, QPoint start, int startDir)
{
    QVector<QPoint> contour;
    if (!isFg(bin, start.x(), start.y()))
        return contour;

    QPoint cur = start;
    int dir = startDir;
    contour.append(cur);

    // 安全上限：周长不会超过像素数的若干倍
    const int limit = bin.width() * bin.height() * 4 + 8;
    for (int step = 0; step < limit; ++step) {
        // 从 dir+1 起逆时针搜下一个前景邻（标准 Moore：背向前进方向左转扫）
        bool found = false;
        for (int k = 0; k < 8; ++k) {
            const int nd = (dir + 1 + k) & 7;
            const int nx = cur.x() + kDx[nd];
            const int ny = cur.y() + kDy[nd];
            if (isFg(bin, nx, ny)) {
                // 下一搜索起点：背向刚走来的方向（nd 的对面）
                dir = (nd + 4) & 7;
                cur = QPoint(nx, ny);
                found = true;
                break;
            }
        }
        if (!found)
            break;
        if (cur == start && contour.size() > 2)
            break;
        contour.append(cur);
    }
    return contour;
}

bool isEdgePixel(const QImage &bin, int x, int y)
{
    if (!isFg(bin, x, y))
        return false;
    for (int k = 0; k < 8; ++k) {
        if (!isFg(bin, x + kDx[k], y + kDy[k]))
            return true;
    }
    return false;
}

qreal periAreaSigned(const QVector<QPoint> &poly)
{
    // 鞋带公式（图像坐标 y 向下）
    qreal a = 0;
    const int n = poly.size();
    if (n < 3)
        return 0;
    for (int i = 0; i < n; ++i) {
        const QPoint &p = poly[i];
        const QPoint &q = poly[(i + 1) % n];
        a += qreal(p.x()) * q.y() - qreal(q.x()) * p.y();
    }
    return a * 0.5;
}

void reversePoly(QVector<QPoint> *poly)
{
    std::reverse(poly->begin(), poly->end());
}

qreal pointSegDist(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 < 1e-18)
        return QLineF(p, a).length();
    qreal t = QPointF::dotProduct(p - a, ab) / ab2;
    t = qBound(0.0, t, 1.0);
    return QLineF(p, a + t * ab).length();
}

void douglasRec(const QVector<QPointF> &in, int i0, int i1, qreal eps,
                QVector<bool> *keep)
{
    if (i1 <= i0 + 1)
        return;
    qreal worst = -1;
    int worstI = -1;
    for (int i = i0 + 1; i < i1; ++i) {
        const qreal d = pointSegDist(in[i], in[i0], in[i1]);
        if (d > worst) {
            worst = d;
            worstI = i;
        }
    }
    if (worstI >= 0 && worst > eps) {
        (*keep)[worstI] = true;
        douglasRec(in, i0, worstI, eps, keep);
        douglasRec(in, worstI, i1, eps, keep);
    }
}

QVector<QPointF> douglasPeuckerClosed(const QVector<QPoint> &poly, qreal eps)
{
    const int n = poly.size();
    if (n < 3)
        return {};
    QVector<QPointF> pts;
    pts.reserve(n);
    for (const QPoint &p : poly)
        pts.append(QPointF(p));

    // 闭合：去掉与首点重合的末点再简化，最后再闭合
    if (pts.size() > 1 && pts.first() == pts.last())
        pts.removeLast();
    if (pts.size() < 3)
        return pts;

    QVector<bool> keep(pts.size(), false);
    keep[0] = true;
    keep[pts.size() - 1] = true;
    douglasRec(pts, 0, pts.size() - 1, eps, &keep);

    QVector<QPointF> out;
    for (int i = 0; i < pts.size(); ++i) {
        if (keep[i])
            out.append(pts[i]);
    }
    return out;
}

bool pointInPoly(const QPointF &p, const QVector<QPoint> &poly)
{
    // 射线法
    bool inside = false;
    const int n = poly.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPoint &a = poly[i];
        const QPoint &b = poly[j];
        const bool intersect = ((a.y() > p.y()) != (b.y() > p.y()))
                               && (p.x()
                                   < (qreal(b.x() - a.x()) * (p.y() - a.y())
                                          / qreal(b.y() - a.y() + 1e-18)
                                      + a.x()));
        if (intersect)
            inside = !inside;
    }
    return inside;
}

SubPath polyToSubPath(const QVector<QPointF> &pts, bool hole)
{
    SubPath sp;
    sp.closed = true;
    for (const QPointF &p : pts) {
        PathAnchor a;
        a.point = p;
        a.inHandle = p;
        a.outHandle = p;
        sp.anchors.append(a);
    }
    // 洞用反向绕序，配合 WindingFill 挖空
    if (hole && sp.anchors.size() >= 2)
        std::reverse(sp.anchors.begin(), sp.anchors.end());
    return sp;
}

} // namespace

Path SelectionToPath::fromMask(const QImage &grayMask, qreal simplifyEpsilon)
{
    Path path;
    path.name = QStringLiteral("工作路径");
    if (grayMask.isNull() || grayMask.width() < 1 || grayMask.height() < 1)
        return path;

    const QImage bin = binarize(grayMask);
    const int w = bin.width();
    const int h = bin.height();
    QBitArray used(w * h, false);

    struct ContourInfo {
        QVector<QPoint> raw;
        qreal absArea = 0;
    };
    QVector<ContourInfo> contours;

    for (int y = 0; y < h; ++y) {
        const uchar *line = bin.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (!line[x] || used.testBit(y * w + x) || !isEdgePixel(bin, x, y))
                continue;
            // 起点：优先从西侧为背景的边界进入（dir=4 表示从西来）
            int startDir = 4;
            for (int k = 0; k < 8; ++k) {
                if (!isFg(bin, x + kDx[k], y + kDy[k])) {
                    startDir = k;
                    break;
                }
            }
            QVector<QPoint> c = traceMoore(bin, QPoint(x, y), startDir);
            if (c.size() < 3)
                continue;
            for (const QPoint &p : c) {
                if (p.x() >= 0 && p.y() >= 0 && p.x() < w && p.y() < h)
                    used.setBit(p.y() * w + p.x(), true);
            }
            ContourInfo info;
            info.raw = c;
            info.absArea = qAbs(periAreaSigned(c));
            if (info.absArea >= 2.0)
                contours.append(info);
        }
    }

    if (contours.isEmpty())
        return path;

    // 面积降序：大轮廓更可能是外环
    std::sort(contours.begin(), contours.end(),
              [](const ContourInfo &a, const ContourInfo &b) {
                  return a.absArea > b.absArea;
              });

    for (int i = 0; i < contours.size(); ++i) {
        QVector<QPoint> raw = contours[i].raw;
        // 统一外环为正面积（图像坐标下鞋带>0）；洞稍后翻转
        if (periAreaSigned(raw) < 0)
            reversePoly(&raw);

        bool hole = false;
        QPointF sample(0, 0);
        for (const QPoint &p : raw) {
            sample += QPointF(p);
        }
        sample /= qMax(1, raw.size());
        for (int j = 0; j < i; ++j) {
            if (pointInPoly(sample, contours[j].raw)) {
                hole = true;
                break;
            }
        }

        QVector<QPointF> simplified = douglasPeuckerClosed(raw, simplifyEpsilon);
        if (simplified.size() < 3)
            continue;
        SubPath sp = polyToSubPath(simplified, hole);
        path.subPaths.append(sp);
        path.recomputeAutoHandles(path.subPaths.size() - 1, 1.0);
    }

    return path;
}

} // namespace Ps
