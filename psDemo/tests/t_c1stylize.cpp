/**
 * t_c1stylize.cpp — C1：风 / 等高线 oracle + 分块投影 + 往返。
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

    return r.fails;
}
