/**
 * t_hpblur.cpp — 高通保留 / 运动模糊：语义 oracle + 分块投影护栏 + 往返。
 *
 * 守的契约：
 *  · 高通 = `clamp(src - boxblur(src,r) + 128)`（逐通道，alpha 保留）——
 *    这里用**生产盒模糊**当参照：模糊本身已由 `areafilter` 的朴素 oracle 钉住，
 *    本用例专注核验「组合式子」对不对；
 *  · 运动模糊 = 一维盒模糊沿 0°/45°/90°，与**朴素一维盒**逐像素一致（同一取整口径）；
 *  · 两者都是面积算子：分块投影 == 整幅（pad 不够会在 64×64 块界出现接缝）；
 *  · 参数存读往返保真。
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

/** 朴素一维盒模糊（水平 / 垂直；abyss=0，除以完整窗口 n）—— 运动模糊的语义 oracle。 */
QImage naiveMotion(const QImage &src, int r, int angle)
{
    const int w = src.width();
    const int h = src.height();
    const int n = 2 * r + 1;

    // 先整体取直通值，再做一/两趟一维盒，最后预乘（与实现同口径）
    auto toStraight = [](const QImage &img) {
        QImage out(img.size(), QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                out.setPixel(x, y, qUnpremultiply(img.pixel(x, y)));
        return out;
    };

    QImage work = toStraight(src);

    const auto boxH = [&](const QImage &in) {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int xx = x + k;
                    if (xx < 0 || xx >= w)
                        continue;
                    const QRgb p = in.pixel(xx, y);
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                out.setPixel(x, y, qRgba(R / n, G / n, B / n, A / n));
            }
        }
        return out;
    };
    const auto boxV = [&](const QImage &in) {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int R = 0, G = 0, B = 0, A = 0;
                for (int k = -r; k <= r; ++k) {
                    const int yy = y + k;
                    if (yy < 0 || yy >= h)
                        continue;
                    const QRgb p = in.pixel(x, yy);
                    R += qRed(p); G += qGreen(p); B += qBlue(p); A += qAlpha(p);
                }
                out.setPixel(x, y, qRgba(R / n, G / n, B / n, A / n));
            }
        }
        return out;
    };

    if (angle == 0 || angle == 45)
        work = boxH(work);
    if (angle == 90 || angle == 45)
        work = boxV(work);

    QImage out(src.size(), src.format());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.setPixel(x, y, qPremultiply(work.pixel(x, y)));
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

        // 参照：用**生产盒模糊**算 blur，再按式子组合
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

        // 分块投影 == 整幅
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

    // —— ② 运动模糊：0°/45°/90° 三档 == 朴素一维盒 ——
    {
        const int W = 40, H = 32, R = 3;
        const QImage src = PsTests::makeLayerImage(W, H, 19, 255);
        for (int angle : {0, 45, 90}) {
            QImage got = src;
            FilterNode node(OpName::MotionBlur);
            node.setMotionDistance(R);
            node.setMotionAngle(angle);
            FilterEval::applyNode(got, node);
            const QImage ref = naiveMotion(src, R, angle);
            const long long d = PsTests::diffCount(got, ref);
            r.check(d == 0, QStringLiteral("运动模糊 %1°：== 朴素一维盒（diffs=%2）")
                                .arg(angle).arg(d));
        }

        // 分块投影 == 整幅（45° 最吃 pad：水平 + 垂直都要够）
        // **0°/90° 也必须测**：它们走的是 ROI 通用化第 ② 步的**不对称**输入需求
        //（只扩一个方向），条带并行的边界取法一旦算错，就会在这里暴露成块界接缝。
        for (int angle : {0, 90, 45}) {
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

        // 输入需求：0°/90° 必须只扩一个方向（这是第 ② 步换来的收益，别被人改回对称）
        {
            FilterNode h(OpName::MotionBlur);
            h.setMotionDistance(10);
            h.setMotionAngle(0);
            const QRect want(100, 100, 64, 64);
            r.check(h.requiredInputs(want) == QRect(90, 100, 84, 64),
                    QStringLiteral("0° 的输入需求只向左扩、上下不扩（实际 %1,%2 %3×%4）")
                        .arg(h.requiredInputs(want).left()).arg(h.requiredInputs(want).top())
                        .arg(h.requiredInputs(want).width()).arg(h.requiredInputs(want).height()));
            h.setMotionAngle(90);
            r.check(h.requiredInputs(want) == QRect(100, 90, 64, 84),
                    QStringLiteral("90° 的输入需求只向上扩、左右不扩"));
            h.setMotionAngle(45);
            r.check(h.requiredInputs(want) == QRect(90, 90, 84, 84),
                    QStringLiteral("45° 两向都扩"));
            // 默认（面积算子）仍是对称外扩 —— 与旧行为逐像素一致
            FilterNode med(OpName::MedianBlur);
            med.setMedianRadius(4);
            r.check(med.requiredInputs(want) == want.adjusted(-4, -4, 4, 4),
                    QStringLiteral("对称算子（中值）仍是四边各扩半径"));
        }

        // 往返（单独一份文档，避免和上面循环的作用域纠缠）
        auto rdoc = ImageDocument::createBlank(120, 90, Qt::white);
        rdoc->placeImageAsLayer(PsTests::makeLayerImage(100, 80, 61, 220), QStringLiteral("mb2"));
        FilterNode rn(OpName::MotionBlur);
        rn.setMotionDistance(9);
        rn.setMotionAngle(45);
        rdoc->layers().layerAt(1)->filters().append(rn);

        const QString path = PsTests::tempFilePath(QStringLiteral("motionblur.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*rdoc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == OpName::MotionBlur && bn.motionDistance() == 9
                        && bn.motionAngle() == 45,
                    QStringLiteral("运动模糊：往返保住距离/角度"));
        } else {
            r.check(false, QStringLiteral("重新打开含运动模糊的 .pslite"));
        }
    }

    return r.fails;
}
