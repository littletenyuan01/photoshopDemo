/**
 * t_hpblur.cpp — 高通保留 / 运动模糊：语义 oracle + 分块投影护栏 + 往返。
 *
 * 运动模糊：0°/90° = 一维盒；其它角度 = 沿方向最近邻取样平均。
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

#include <cmath>

using namespace Ps;
using PsTests::Reporter;

namespace {

QImage toStraight(const QImage &img)
{
    QImage out(img.size(), QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            out.setPixel(x, y, qUnpremultiply(img.pixel(x, y)));
    return out;
}

QImage fromStraight(const QImage &work, QImage::Format fmt)
{
    QImage out(work.size(), fmt);
    for (int y = 0; y < work.height(); ++y)
        for (int x = 0; x < work.width(); ++x)
            out.setPixel(x, y, qPremultiply(work.pixel(x, y)));
    return out;
}

/** 朴素运动模糊 oracle（与生产实现同口径）。 */
QImage naiveMotion(const QImage &src, int r, int angle)
{
    const int w = src.width();
    const int h = src.height();
    const int n = 2 * r + 1;
    const int ang = ((angle % 180) + 180) % 180;
    QImage work = toStraight(src);

    if (ang == 0) {
        QImage out(work.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int xx = x + k;
                    if (xx < 0 || xx >= w)
                        continue;
                    const QRgb p = work.pixel(xx, y);
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                out.setPixel(x, y, qRgba(R / n, G / n, B / n, A / n));
            }
        }
        work = out;
    } else if (ang == 90) {
        QImage out(work.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int yy = y + k;
                    if (yy < 0 || yy >= h)
                        continue;
                    const QRgb p = work.pixel(x, yy);
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                out.setPixel(x, y, qRgba(R / n, G / n, B / n, A / n));
            }
        }
        work = out;
    } else {
        const double rad = double(ang) * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        QImage out(work.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int xx = int(std::lround(x + k * c));
                    const int yy = int(std::lround(y + k * s));
                    if (xx < 0 || xx >= w || yy < 0 || yy >= h)
                        continue;
                    const QRgb p = work.pixel(xx, yy);
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                out.setPixel(x, y, qRgba(R / n, G / n, B / n, A / n));
            }
        }
        work = out;
    }
    return fromStraight(work, src.format());
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

int tHighPassMotion()
{
    Reporter r;

    // —— ① 高通保留 ——
    {
        const int W = 40, H = 32, R = 4;
        const QImage src = PsTests::makeLayerImage(W, H, 13, 255);

        QImage got = src;
        FilterNode node(OpName::HighPass);
        node.setHighPassRadius(R);
        FilterEval::applyNode(got, node);

        QImage blurred = src;
        FilterNode bnode(OpName::BoxBlur);
        bnode.setBlurRadius(R);
        FilterEval::applyNode(blurred, bnode);
        QImage ref(src.size(), src.format());
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const QRgb sp = qUnpremultiply(src.pixel(x, y));
                const QRgb bp = qUnpremultiply(blurred.pixel(x, y));
                ref.setPixel(x, y,
                             qPremultiply(qRgba(qBound(0, qRed(sp) - qRed(bp) + 128, 255),
                                                qBound(0, qGreen(sp) - qGreen(bp) + 128, 255),
                                                qBound(0, qBlue(sp) - qBlue(bp) + 128, 255),
                                                qAlpha(sp))));
            }
        }
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("高通：== clamp(src - 盒模糊 + 128)（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0,
                QStringLiteral("高通确实改变了画面（diffs=%1）").arg(PsTests::diffCount(src, got)));

        const int DW = 256, DH = 192;
        auto doc = ImageDocument::createBlank(DW, DH, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 47, 220), QStringLiteral("hp"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(OpName::HighPass);
        n.setHighPassRadius(6);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("高通：分块投影 == 整幅合成（diffs=%1）").arg(dc));

        const QString path = PsTests::tempFilePath(QStringLiteral("highpass.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::HighPass && bn.highPassRadius() == 6,
                    QStringLiteral("高通：往返保住半径 6"));
        } else {
            r.check(false, QStringLiteral("重新打开含高通的 .pslite"));
        }
    }

    // —— ② 运动模糊：0°/90°/30°/45° ——
    {
        const int W = 40, H = 32, R = 3;
        const QImage src = PsTests::makeLayerImage(W, H, 19, 255);
        for (int angle : {0, 90, 30, 45}) {
            QImage got = src;
            FilterNode node(OpName::MotionBlur);
            node.setMotionDistance(R);
            node.setMotionAngle(angle);
            FilterEval::applyNode(got, node);
            const QImage ref = naiveMotion(src, R, angle);
            const long long d = PsTests::diffCount(got, ref);
            r.check(d == 0, QStringLiteral("运动模糊 %1°：== 朴素实现（diffs=%2）")
                                .arg(angle).arg(d));
        }

        for (int angle : {0, 90, 30, 45}) {
            const int DW = 256, DH = 192;
            auto doc = ImageDocument::createBlank(DW, DH, Qt::white);
            doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 53, 220), QStringLiteral("mb"));
            doc->layers().layerAt(1)->setOffset(20, 10);
            FilterNode n(OpName::MotionBlur);
            n.setMotionDistance(9);
            n.setMotionAngle(angle);
            doc->layers().layerAt(1)->filters().append(n);
            const long long dc =
                PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
            r.check(dc == 0, QStringLiteral("运动模糊 %1°：分块投影 == 整幅合成（diffs=%2）")
                                .arg(angle).arg(dc));
        }

        {
            FilterNode h(OpName::MotionBlur);
            h.setMotionDistance(10);
            h.setMotionAngle(0);
            const QRect want(100, 100, 64, 64);
            r.check(h.requiredInputs(want) == QRect(90, 100, 84, 64),
                    QStringLiteral("0° 的输入需求只向左右扩"));
            h.setMotionAngle(90);
            r.check(h.requiredInputs(want) == QRect(100, 90, 64, 84),
                    QStringLiteral("90° 的输入需求只向上下扩"));
            h.setMotionAngle(45);
            // pad = ceil(10/√2) = 8
            r.check(h.requiredInputs(want) == QRect(92, 92, 80, 80),
                    QStringLiteral("45° 按方向分量外扩（实际 %1,%2 %3×%4）")
                        .arg(h.requiredInputs(want).left()).arg(h.requiredInputs(want).top())
                        .arg(h.requiredInputs(want).width()).arg(h.requiredInputs(want).height()));
        }

        auto rdoc = ImageDocument::createBlank(120, 90, Qt::white);
        rdoc->placeImageAsLayer(PsTests::makeLayerImage(100, 80, 61, 220), QStringLiteral("mb2"));
        FilterNode rn(OpName::MotionBlur);
        rn.setMotionDistance(9);
        rn.setMotionAngle(37);
        rdoc->layers().layerAt(1)->filters().append(rn);

        const QString path = PsTests::tempFilePath(QStringLiteral("motionblur.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*rdoc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::MotionBlur && bn.motionDistance() == 9
                        && bn.motionAngle() == 37,
                    QStringLiteral("运动模糊：往返保住距离/角度"));
        } else {
            r.check(false, QStringLiteral("重新打开含运动模糊的 .pslite"));
        }
    }

    return r.fails;
}
