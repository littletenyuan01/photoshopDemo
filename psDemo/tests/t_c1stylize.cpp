/**
 * t_c1stylize.cpp — C1：风 / 等高线 / 径向 / 表面 / 特殊模糊 / 拼贴 oracle + 分块投影 + 往返。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/filtereval.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QImage>
#include <QtMath>

using namespace Ps;
using PsTests::Reporter;

namespace {

int luma(QRgb p)
{
    return (qRed(p) * 30 + qGreen(p) * 59 + qBlue(p) * 11) / 100;
}

QImage naiveWind(const QImage &srcPremul, int direction, int method, int strength)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    const int baseS = (method == 1) ? (strength * 2) : strength;
    const int baseDir = (direction == 0) ? +1 : -1;

    QImage work(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(srcPremul.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qUnpremultiply(s[x]);
    }

    QImage straight(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const int dir = (method == 2 && (y & 1)) ? -baseDir : baseDir;
        const QRgb *srcRow = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *dstRow = reinterpret_cast<QRgb *>(straight.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb best = srcRow[x];
            int bestL = luma(best);
            for (int k = 1; k <= baseS; ++k) {
                const int sx = x - k * dir;
                if (sx < 0 || sx >= w)
                    break;
                const QRgb p = srcRow[sx];
                const int L = luma(p);
                if (L > bestL) {
                    bestL = L;
                    best = p;
                }
            }
            dstRow[x] = best;
        }
    }

    QImage out(w, h, srcPremul.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

QImage naiveContour(const QImage &srcPremul, int level, int edge)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    QImage work(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(srcPremul.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qUnpremultiply(s[x]);
    }

    auto sampleL = [&](int x, int y) -> int {
        x = qBound(0, x, w - 1);
        y = qBound(0, y, h - 1);
        return luma(reinterpret_cast<const QRgb *>(work.constScanLine(y))[x]);
    };

    QImage straight(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *srcRow = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *dstRow = reinterpret_cast<QRgb *>(straight.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int Lp = luma(srcRow[x]);
            const int a = qAlpha(srcRow[x]);
            bool mark = false;
            const int nbs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (const auto &nb : nbs) {
                const int Ln = sampleL(x + nb[0], y + nb[1]);
                const bool cross = (Lp < level && Ln >= level) || (Lp >= level && Ln < level);
                if (!cross)
                    continue;
                if (edge == 0 || Lp >= level) {
                    mark = true;
                    break;
                }
            }
            dstRow[x] = mark ? qRgba(0, 0, 0, a) : qRgba(255, 255, 255, a);
        }
    }

    QImage out(w, h, srcPremul.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

QImage naiveRadial(const QImage &srcPremul, int amount, int method)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    const double cx = (w - 1) * 0.5;
    const double cy = (h - 1) * 0.5;
    const int N = qBound(2, 4 + amount / 2, 24);

    QImage work(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(srcPremul.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qUnpremultiply(s[x]);
    }

    auto sample = [&](double fx, double fy) -> QRgb {
        const int ix = qBound(0, int(qRound(fx)), w - 1);
        const int iy = qBound(0, int(qRound(fy)), h - 1);
        return reinterpret_cast<const QRgb *>(work.constScanLine(iy))[ix];
    };

    QImage straight(w, h, QImage::Format_ARGB32);
    if (method == 0) {
        const double halfArc = (double(amount) * M_PI / 180.0) * 0.5;
        for (int y = 0; y < h; ++y) {
            QRgb *dst = reinterpret_cast<QRgb *>(straight.scanLine(y));
            for (int x = 0; x < w; ++x) {
                qint64 R = 0, G = 0, B = 0, A = 0;
                const double dx = x - cx;
                const double dy = y - cy;
                for (int i = 0; i < N; ++i) {
                    const double t = (N == 1) ? 0.0 : (double(i) / (N - 1) * 2.0 - 1.0);
                    const double ang = t * halfArc;
                    const double ca = qCos(ang);
                    const double sa = qSin(ang);
                    const QRgb p = sample(cx + dx * ca - dy * sa, cy + dx * sa + dy * ca);
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                }
                dst[x] = qRgba(int(R / N), int(G / N), int(B / N), int(A / N));
            }
        }
    } else {
        const double zoomFrac = double(amount) / 100.0;
        for (int y = 0; y < h; ++y) {
            QRgb *dst = reinterpret_cast<QRgb *>(straight.scanLine(y));
            for (int x = 0; x < w; ++x) {
                qint64 R = 0, G = 0, B = 0, A = 0;
                const double dx = x - cx;
                const double dy = y - cy;
                for (int i = 0; i < N; ++i) {
                    const double t = (N == 1) ? 0.0 : double(i) / (N - 1);
                    const double s = 1.0 - t * zoomFrac;
                    const QRgb p = sample(cx + dx * s, cy + dy * s);
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                }
                dst[x] = qRgba(int(R / N), int(G / N), int(B / N), int(A / N));
            }
        }
    }

    QImage out(w, h, srcPremul.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

int chanDiff(QRgb a, QRgb b)
{
    return qMax(qAbs(qRed(a) - qRed(b)),
                qMax(qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b))));
}

QImage toStraight(const QImage &srcPremul)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(srcPremul.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qUnpremultiply(s[x]);
    }
    return out;
}

QImage toPremul(const QImage &straight, QImage::Format fmt)
{
    const int w = straight.width();
    const int h = straight.height();
    QImage out(w, h, fmt);
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

QImage naiveSurfaceBlur(const QImage &srcPremul, int radius, int threshold)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    const int r = radius;
    const int thr = qMax(0, threshold);
    const QImage src = toStraight(srcPremul);
    QImage dst = src;
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb center = reinterpret_cast<const QRgb *>(src.constScanLine(y))[x];
            qint64 R = 0, G = 0, B = 0, A = 0;
            int n = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb *srow = reinterpret_cast<const QRgb *>(src.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    const QRgb p = srow[xx];
                    if (chanDiff(center, p) > thr)
                        continue;
                    R += qRed(p);
                    G += qGreen(p);
                    B += qBlue(p);
                    A += qAlpha(p);
                    ++n;
                }
            }
            row[x] = (n <= 0) ? center
                              : qRgba(int(R / n), int(G / n), int(B / n), int(A / n));
        }
    }
    return toPremul(dst, srcPremul.format());
}

QImage naiveEdges(const QImage &straight, int radius, int threshold)
{
    const int w = straight.width();
    const int h = straight.height();
    const int r = qMax(1, radius);
    const int thr = qMax(0, threshold);
    QImage dst(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(dst.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb center = reinterpret_cast<const QRgb *>(straight.constScanLine(y))[x];
            const int a = qAlpha(center);
            bool edge = false;
            for (int dy = -r; dy <= r && !edge; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb *srow = reinterpret_cast<const QRgb *>(straight.constScanLine(yy));
                for (int dx = -r; dx <= r; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w)
                        continue;
                    if (chanDiff(center, srow[xx]) > thr) {
                        edge = true;
                        break;
                    }
                }
            }
            row[x] = edge ? qRgba(255, 255, 255, a) : qRgba(0, 0, 0, a);
        }
    }
    return dst;
}

QImage naiveSmartBlur(const QImage &srcPremul, int radius, int threshold, int mode)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    QImage work = toStraight(srcPremul);
    if (mode == 1) {
        work = naiveEdges(work, radius, threshold);
    } else if (mode == 2) {
        QImage edges = naiveEdges(work, radius, threshold);
        work = toStraight(naiveSurfaceBlur(srcPremul, radius, threshold));
        for (int y = 0; y < h; ++y) {
            QRgb *d = reinterpret_cast<QRgb *>(work.scanLine(y));
            const QRgb *e = reinterpret_cast<const QRgb *>(edges.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                if (qRed(e[x]) > 128)
                    d[x] = qRgba(0, 0, 0, qAlpha(d[x]));
            }
        }
    } else {
        return naiveSurfaceBlur(srcPremul, radius, threshold);
    }
    return toPremul(work, srcPremul.format());
}

quint32 tileHash(int tx, int ty, quint32 seed)
{
    quint32 h = seed ^ (quint32(tx) * 0x9E3779B9u) ^ (quint32(ty) * 0x85EBCA6Bu);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

int signedOffset(quint32 h, int maxAbs)
{
    if (maxAbs <= 0)
        return 0;
    const int span = maxAbs * 2 + 1;
    return int(h % quint32(span)) - maxAbs;
}

QImage naiveTiles(const QImage &srcPremul, int count, int maxOffset, int fill, quint32 seed)
{
    const int w = srcPremul.width();
    const int h = srcPremul.height();
    const int n = qBound(2, count, 100);
    QImage src = toStraight(srcPremul);
    QImage dst(w, h, QImage::Format_ARGB32);
    if (fill == 1) {
        for (int y = 0; y < h; ++y) {
            const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = s[x];
                d[x] = qRgba(255 - qRed(p), 255 - qGreen(p), 255 - qBlue(p), qAlpha(p));
            }
        }
    } else if (fill == 2) {
        dst = src.copy();
    } else {
        dst.fill(qRgba(0, 0, 0, 0));
    }
    for (int ty = 0; ty < n; ++ty) {
        const int y0 = (ty * h) / n;
        const int y1 = ((ty + 1) * h) / n;
        const int th = y1 - y0;
        if (th <= 0)
            continue;
        const int maxOy = (th * maxOffset) / 100;
        for (int tx = 0; tx < n; ++tx) {
            const int x0 = (tx * w) / n;
            const int x1 = ((tx + 1) * w) / n;
            const int tw = x1 - x0;
            if (tw <= 0)
                continue;
            const int maxOx = (tw * maxOffset) / 100;
            const int ox = signedOffset(tileHash(tx, ty, seed), maxOx);
            const int oy = signedOffset(tileHash(tx + 17, ty + 31, seed ^ 0xA5A5A5A5u), maxOy);
            for (int sy = y0; sy < y1; ++sy) {
                const int dy = sy + oy;
                if (dy < 0 || dy >= h)
                    continue;
                const QRgb *srow = reinterpret_cast<const QRgb *>(src.constScanLine(sy));
                QRgb *drow = reinterpret_cast<QRgb *>(dst.scanLine(dy));
                for (int sx = x0; sx < x1; ++sx) {
                    const int dx = sx + ox;
                    if (dx < 0 || dx >= w)
                        continue;
                    drow[dx] = srow[sx];
                }
            }
        }
    }
    return toPremul(dst, srcPremul.format());
}

QImage chunkedProjection(ImageDocument *doc)
{
    Projection p;
    p.bind(doc);
    p.setPriorityRect(QRect(0, 0, doc->width(), doc->height()));
    doc->markDirty();
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);
    return p.image();
}

} // namespace

int tC1Stylize()
{
    Reporter r;

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 19, 255);
        FilterNode n(OpName::Wind);
        n.setWindDirection(0);
        n.setWindMethod(0);
        n.setWindStrength(4);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveWind(src, 0, 0, 4));
        r.check(d == 0, QStringLiteral("风 == 朴素上风向最大亮度（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("风确实改变画面"));
        r.check(n.inputPad() == 4, QStringLiteral("风 inputPad==strength"));
    }

    {
        const QImage src = PsTests::makeLayerImage(40, 40, 11, 255);
        FilterNode n(OpName::Wind);
        n.setWindDirection(1);
        n.setWindMethod(2);
        n.setWindStrength(3);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveWind(src, 1, 2, 3));
        r.check(d == 0, QStringLiteral("风交错 == 朴素（diffs=%1）").arg(d));
        const QRect want(8, 8, 16, 16);
        const QRect need = n.requiredInputs(want);
        r.check(need == want.adjusted(-3, 0, 3, 0),
                QStringLiteral("风交错 requiredInputs 两侧扩 strength"));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 21, 255), QStringLiteral("wind"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::Wind);
        n.setWindStrength(5);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("风：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(80, 60, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(60, 48, 3, 255), QStringLiteral("wind2"));
        FilterNode n(OpName::Wind);
        n.setWindDirection(1);
        n.setWindMethod(1);
        n.setWindStrength(7);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("wind.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::Wind && bn.windDirection() == 1 && bn.windMethod() == 1
                        && bn.windStrength() == 7,
                    QStringLiteral("风：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含风的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 23, 255);
        FilterNode n(OpName::TraceContour);
        n.setContourLevel(100);
        n.setContourEdge(0);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveContour(src, 100, 0));
        r.check(d == 0, QStringLiteral("等高线较低 == 朴素（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("等高线确实改变画面"));
        r.check(n.inputPad() == 1, QStringLiteral("等高线 inputPad=1"));
    }

    {
        const QImage src = PsTests::makeLayerImage(40, 40, 5, 255);
        FilterNode n(OpName::TraceContour);
        n.setContourLevel(140);
        n.setContourEdge(1);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveContour(src, 140, 1));
        r.check(d == 0, QStringLiteral("等高线较高 == 朴素（diffs=%1）").arg(d));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 33, 255), QStringLiteral("ctr"));
        doc->layers().layerAt(1)->setOffset(16, 12);
        FilterNode n(OpName::TraceContour);
        n.setContourLevel(128);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("等高线：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 5, 255), QStringLiteral("ctr2"));
        FilterNode n(OpName::TraceContour);
        n.setContourLevel(90);
        n.setContourEdge(1);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("contour.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::TraceContour && bn.contourLevel() == 90
                        && bn.contourEdge() == 1,
                    QStringLiteral("等高线：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含等高线的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 29, 255);
        FilterNode n(OpName::RadialBlur);
        n.setRadialAmount(8);
        n.setRadialMethod(0);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveRadial(src, 8, 0));
        r.check(d == 0, QStringLiteral("径向旋转 == 朴素（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("径向模糊确实改变画面"));
        r.check(n.inputPad() == 0, QStringLiteral("径向模糊 inputPad=0（整层）"));
    }

    {
        const QImage src = PsTests::makeLayerImage(40, 40, 17, 255);
        FilterNode n(OpName::RadialBlur);
        n.setRadialAmount(12);
        n.setRadialMethod(1);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveRadial(src, 12, 1));
        r.check(d == 0, QStringLiteral("径向缩放 == 朴素（diffs=%1）").arg(d));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 41, 255), QStringLiteral("rad"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::RadialBlur);
        n.setRadialAmount(6);
        doc->layers().layerAt(1)->filters().append(n);
        r.check(doc->layers().layerAt(1)->filters().hasPositionalOp(),
                QStringLiteral("径向模糊走位置相关（整层）路径"));
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("径向模糊：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 5, 255), QStringLiteral("rad2"));
        FilterNode n(OpName::RadialBlur);
        n.setRadialAmount(15);
        n.setRadialMethod(1);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("radial.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::RadialBlur && bn.radialAmount() == 15
                        && bn.radialMethod() == 1,
                    QStringLiteral("径向模糊：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含径向模糊的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(32, 24, 61, 255);
        FilterNode n(OpName::SurfaceBlur);
        n.setSurfaceRadius(3);
        n.setSurfaceThreshold(40);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveSurfaceBlur(src, 3, 40));
        r.check(d == 0, QStringLiteral("表面模糊 == 朴素（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("表面模糊确实改变画面"));
        r.check(n.inputPad() == 3, QStringLiteral("表面模糊 inputPad=半径"));
    }

    {
        auto doc = ImageDocument::createBlank(128, 96, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(100, 80, 73, 255), QStringLiteral("surf"));
        doc->layers().layerAt(1)->setOffset(10, 8);
        FilterNode n(OpName::SurfaceBlur);
        n.setSurfaceRadius(2);
        n.setSurfaceThreshold(30);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("表面模糊：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 11, 255), QStringLiteral("surf2"));
        FilterNode n(OpName::SurfaceBlur);
        n.setSurfaceRadius(7);
        n.setSurfaceThreshold(55);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("surface.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::SurfaceBlur && bn.surfaceRadius() == 7
                        && bn.surfaceThreshold() == 55,
                    QStringLiteral("表面模糊：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含表面模糊的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(28, 22, 83, 255);
        for (int mode = 0; mode <= 2; ++mode) {
            FilterNode n(OpName::SmartBlur);
            n.setSmartRadius(2);
            n.setSmartThreshold(35);
            n.setSmartMode(mode);
            QImage got = src;
            FilterEval::applyNode(got, n);
            const long long d = PsTests::diffCount(got, naiveSmartBlur(src, 2, 35, mode));
            r.check(d == 0,
                    QStringLiteral("特殊模糊 mode=%1 == 朴素（diffs=%2）").arg(mode).arg(d));
        }
        r.check(FilterNode(OpName::SmartBlur).inputPad() == 3,
                QStringLiteral("特殊模糊默认 inputPad=3"));
    }

    {
        auto doc = ImageDocument::createBlank(96, 72, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(80, 60, 97, 255), QStringLiteral("smart"));
        doc->layers().layerAt(1)->setOffset(8, 6);
        FilterNode n(OpName::SmartBlur);
        n.setSmartRadius(2);
        n.setSmartThreshold(28);
        n.setSmartMode(2);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("特殊模糊：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 19, 255), QStringLiteral("smart2"));
        FilterNode n(OpName::SmartBlur);
        n.setSmartRadius(4);
        n.setSmartThreshold(42);
        n.setSmartMode(1);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("smart.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::SmartBlur && bn.smartRadius() == 4
                        && bn.smartThreshold() == 42 && bn.smartMode() == 1,
                    QStringLiteral("特殊模糊：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含特殊模糊的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 103, 255);
        FilterNode n(OpName::Tiles);
        n.setTilesCount(4);
        n.setTilesMaxOffset(40);
        n.setTilesFill(0);
        n.setTilesSeed(0xDEADBEEFu);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d =
            PsTests::diffCount(got, naiveTiles(src, 4, 40, 0, 0xDEADBEEFu));
        r.check(d == 0, QStringLiteral("拼贴透明填充 == 朴素（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("拼贴确实改变画面"));
        r.check(n.inputPad() == 0, QStringLiteral("拼贴 inputPad=0（整层）"));
    }

    {
        const QImage src = PsTests::makeLayerImage(40, 40, 109, 255);
        FilterNode n(OpName::Tiles);
        n.setTilesCount(5);
        n.setTilesMaxOffset(25);
        n.setTilesFill(1);
        n.setTilesSeed(42u);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveTiles(src, 5, 25, 1, 42u));
        r.check(d == 0, QStringLiteral("拼贴反相填充 == 朴素（diffs=%1）").arg(d));
    }

    {
        auto doc = ImageDocument::createBlank(128, 96, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(100, 80, 113, 255), QStringLiteral("tiles"));
        doc->layers().layerAt(1)->setOffset(12, 8);
        FilterNode n(OpName::Tiles);
        n.setTilesCount(6);
        n.setTilesMaxOffset(30);
        n.setTilesFill(2);
        doc->layers().layerAt(1)->filters().append(n);
        r.check(doc->layers().layerAt(1)->filters().hasPositionalOp(),
                QStringLiteral("拼贴走位置相关（整层）路径"));
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("拼贴：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 127, 255), QStringLiteral("tiles2"));
        FilterNode n(OpName::Tiles);
        n.setTilesCount(8);
        n.setTilesMaxOffset(15);
        n.setTilesFill(1);
        n.setTilesSeed(0xABCDu);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("tiles.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::Tiles && bn.tilesCount() == 8 && bn.tilesMaxOffset() == 15
                        && bn.tilesFill() == 1 && bn.tilesSeed() == 0xABCDu,
                    QStringLiteral("拼贴：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含拼贴的 .pslite"));
        }
    }

    return r.fails;
}
