/**
 * t_c1pixelate.cpp — C1：马赛克 / 碎片 oracle + 分块投影 + 往返。
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

QImage naiveMosaic(const QImage &srcPremul, int cell)
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
    for (int cy = 0; cy < h; cy += cell) {
        const int y1 = qMin(cy + cell, h);
        for (int cx = 0; cx < w; cx += cell) {
            const int x1 = qMin(cx + cell, w);
            qint64 R = 0, G = 0, B = 0, A = 0;
            int n = 0;
            for (int y = cy; y < y1; ++y) {
                const QRgb *row = reinterpret_cast<const QRgb *>(work.constScanLine(y));
                for (int x = cx; x < x1; ++x) {
                    R += qRed(row[x]);
                    G += qGreen(row[x]);
                    B += qBlue(row[x]);
                    A += qAlpha(row[x]);
                    ++n;
                }
            }
            const QRgb avg = qRgba(int(R / n), int(G / n), int(B / n), int(A / n));
            for (int y = cy; y < y1; ++y) {
                QRgb *row = reinterpret_cast<QRgb *>(work.scanLine(y));
                for (int x = cx; x < x1; ++x)
                    row[x] = avg;
            }
        }
    }
    QImage out(w, h, srcPremul.format());
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(work.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
    return out;
}

QImage naiveFragment(const QImage &src, int o)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            auto sample = [&](int xx, int yy) -> QRgb {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                    return 0;
                return reinterpret_cast<const QRgb *>(src.constScanLine(yy))[xx];
            };
            const QRgb a = sample(x - o, y - o);
            const QRgb b = sample(x + o, y - o);
            const QRgb c = sample(x - o, y + o);
            const QRgb e = sample(x + o, y + o);
            d[x] = qRgba((qRed(a) + qRed(b) + qRed(c) + qRed(e)) / 4,
                         (qGreen(a) + qGreen(b) + qGreen(c) + qGreen(e)) / 4,
                         (qBlue(a) + qBlue(b) + qBlue(c) + qBlue(e)) / 4,
                         (qAlpha(a) + qAlpha(b) + qAlpha(c) + qAlpha(e)) / 4);
        }
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

int tC1Pixelate()
{
    Reporter r;

    {
        const QImage src = PsTests::makeLayerImage(48, 36, 17, 255);
        FilterNode n(OpName::Mosaic);
        n.setMosaicCell(8);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveMosaic(src, 8));
        r.check(d == 0, QStringLiteral("马赛克 == 朴素方块均值（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("马赛克确实改变画面"));
    }

    {
        FilterNode n(OpName::Mosaic);
        n.setMosaicCell(4);
        r.check(n.inputPad() == 0, QStringLiteral("马赛克 inputPad=0（整层路径）"));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 21, 220), QStringLiteral("mos"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::Mosaic);
        n.setMosaicCell(10);
        doc->layers().layerAt(1)->filters().append(n);
        r.check(doc->layers().layerAt(1)->filters().hasPositionalOp(),
                QStringLiteral("马赛克走位置相关（整层）路径"));
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("马赛克：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(80, 60, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(60, 48, 3, 255), QStringLiteral("mos2"));
        FilterNode n(OpName::Mosaic);
        n.setMosaicCell(12);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("mosaic.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::Mosaic && bn.mosaicCell() == 12,
                    QStringLiteral("马赛克参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含马赛克的 .pslite"));
        }
    }

    {
        const QImage src = PsTests::makeLayerImage(40, 32, 9, 255);
        FilterNode n(OpName::Fragment);
        QImage got = src;
        FilterEval::applyNode(got, n);
        const long long d = PsTests::diffCount(got, naiveFragment(src, 4));
        r.check(d == 0, QStringLiteral("碎片 == 朴素四角平均（diffs=%1）").arg(d));
        r.check(n.fragmentOffset() == 4 && n.inputPad() == 4,
                QStringLiteral("碎片默认偏移=4 / inputPad=4"));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("碎片确实改变画面"));
    }

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 33, 255), QStringLiteral("frag"));
        doc->layers().layerAt(1)->setOffset(16, 12);
        FilterNode n(OpName::Fragment);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("碎片：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(48, 36, 5, 255), QStringLiteral("frag2"));
        FilterNode n(OpName::Fragment);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("fragment.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            r.check(back->layers().layerAt(1)->filters().at(0).op() == OpName::Fragment,
                    QStringLiteral("碎片 op 往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含碎片的 .pslite"));
        }
    }

    return r.fails;
}
