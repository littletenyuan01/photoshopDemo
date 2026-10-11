/**
 * t_batcha.cpp — 滤镜批次 A：曝光过度 / 蒙尘与划痕 / 减少杂色 / 锐化边缘。
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

QImage naiveSolarize(QImage src)
{
    for (int y = 0; y < src.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(src.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const QRgb p = qUnpremultiply(line[x]);
            auto flip = [](int v) { return v > 128 ? 255 - v : v; };
            line[x] = qPremultiply(qRgba(flip(qRed(p)), flip(qGreen(p)), flip(qBlue(p)), qAlpha(p)));
        }
    }
    return src;
}

} // namespace

int tBatchA()
{
    Reporter r;

    // ① Solarize oracle
    {
        const QImage src = PsTests::makeLayerImage(48, 32, 17, 255);
        QImage got = src;
        FilterEval::applyNode(got, FilterNode(OpName::Solarize));
        const QImage ref = naiveSolarize(src.copy());
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("Solarize == 朴素实现（diffs=%1）").arg(d));
    }

    // ② Dust：阈值 255 ≈ 恒等；阈值 0 == 中值
    {
        const QImage src = PsTests::makeLayerImage(40, 32, 23, 255);
        QImage id = src;
        FilterNode dustHi(OpName::DustAndScratches);
        dustHi.setMedianRadius(1);
        dustHi.setThreshold(255);
        FilterEval::applyNode(id, dustHi);
        r.check(PsTests::diffCount(id, src) == 0, QStringLiteral("蒙尘阈值 255 ≈ 恒等"));

        QImage dust0 = src;
        FilterNode d0(OpName::DustAndScratches);
        d0.setMedianRadius(1);
        d0.setThreshold(0);
        FilterEval::applyNode(dust0, d0);
        QImage med = src;
        FilterNode m(OpName::MedianBlur);
        m.setMedianRadius(1);
        FilterEval::applyNode(med, m);
        const long long d = PsTests::diffCount(dust0, med);
        r.check(d == 0, QStringLiteral("蒙尘阈值 0 == 中值 r=1（diffs=%1）").arg(d));
    }

    // ③ ReduceNoise：0 恒等；100 == 中值
    {
        const QImage src = PsTests::makeLayerImage(40, 32, 29, 255);
        QImage z = src;
        FilterNode rz(OpName::ReduceNoise);
        rz.setMedianRadius(2);
        rz.setReduceNoiseStrength(0);
        FilterEval::applyNode(z, rz);
        r.check(PsTests::diffCount(z, src) == 0, QStringLiteral("减少杂色强度 0 == 恒等"));

        QImage full = src;
        FilterNode rf(OpName::ReduceNoise);
        rf.setMedianRadius(2);
        rf.setReduceNoiseStrength(100);
        FilterEval::applyNode(full, rf);
        QImage med = src;
        FilterNode m(OpName::MedianBlur);
        m.setMedianRadius(2);
        FilterEval::applyNode(med, m);
        const long long d = PsTests::diffCount(full, med);
        r.check(d == 0, QStringLiteral("减少杂色强度 100 == 中值（diffs=%1）").arg(d));
    }

    // ④ SharpenEdges：平坦色内部恒等（边界因 abyss=0 会有假边缘，不比）
    {
        QImage flat(32, 24, QImage::Format_ARGB32_Premultiplied);
        flat.fill(qPremultiply(qRgba(120, 90, 60, 255)));
        QImage out = flat;
        FilterNode n(OpName::SharpenEdges);
        n.setSharpenStrength(2);
        FilterEval::applyNode(out, n);
        long long interiorDiff = 0;
        for (int y = 1; y < flat.height() - 1; ++y) {
            for (int x = 1; x < flat.width() - 1; ++x) {
                if (out.pixel(x, y) != flat.pixel(x, y))
                    ++interiorDiff;
            }
        }
        r.check(interiorDiff == 0,
                QStringLiteral("锐化边缘在平坦色内部恒等（diffs=%1）").arg(interiorDiff));
    }

    // ⑤ 分块投影 == 整幅
    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 47, 220), QStringLiteral("dust"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::DustAndScratches);
        n.setMedianRadius(1);
        n.setThreshold(20);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("蒙尘：分块投影 == 整幅（diffs=%1）").arg(dc));
    }
    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 53, 220), QStringLiteral("rn"));
        FilterNode n(OpName::ReduceNoise);
        n.setMedianRadius(2);
        n.setReduceNoiseStrength(60);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("减少杂色：分块投影 == 整幅（diffs=%1）").arg(dc));
    }
    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 59, 220), QStringLiteral("se"));
        FilterNode n(OpName::SharpenEdges);
        n.setSharpenStrength(2);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("锐化边缘：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    // ⑥ 参数往返
    {
        auto doc = ImageDocument::createBlank(64, 48, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(40, 32, 11, 255), QStringLiteral("rn"));
        FilterNode n(OpName::ReduceNoise);
        n.setMedianRadius(3);
        n.setReduceNoiseStrength(77);
        doc->layers().layerAt(1)->filters().append(n);
        const QString path = PsTests::tempFilePath(QStringLiteral("batcha.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::ReduceNoise && bn.medianRadius() == 3
                        && bn.reduceNoiseStrength() == 77,
                    QStringLiteral("减少杂色参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含减少杂色的 .pslite"));
        }
    }

    return r.fails;
}
