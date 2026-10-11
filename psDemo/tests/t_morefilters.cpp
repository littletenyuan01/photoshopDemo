/**
 * t_morefilters.cpp — 最大值 / 最小值 / 自定滤镜：语义 oracle + 分块投影护栏 + 往返。
 *
 * 守的契约：
 *  · 最大值 = 窗口内 max、最小值 = 窗口内 min（abyss=透明 0，与中值/盒模糊同口径）；
 *  · 自定滤镜 = 3×3 卷积 `clamp(Σ w·in / scale + offset)`，alpha 原样保留；
 *  · 三者都是面积算子：**分块投影 == 整幅合成**（pad 不够就会在 64×64 块界出现接缝）；
 *  · 参数（半径 / 权重+缩放+位移）存读往返保真。
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
#include <QVector>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 朴素极值滤波（逐像素取窗口 max/min，abyss=0）—— max/min 的语义 oracle。 */
QImage naiveExtremum(const QImage &src, int r, bool takeMax)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(src.size(), src.format());
    const auto ch = [&src, w, h](int x, int y, int c) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return 0;
        const QRgb p = qUnpremultiply(src.pixel(x, y));
        return (c == 0) ? qRed(p) : ((c == 1) ? qGreen(p) : ((c == 2) ? qBlue(p) : qAlpha(p)));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int v[4];
            for (int c = 0; c < 4; ++c) {
                int acc = takeMax ? 0 : 255;
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        const int s = ch(x + dx, y + dy, c);
                        acc = takeMax ? qMax(acc, s) : qMin(acc, s);
                    }
                v[c] = acc;
            }
            out.setPixel(x, y, qPremultiply(qRgba(v[0], v[1], v[2], v[3])));
        }
    }
    return out;
}

/** 朴素 3×3 卷积（abyss=0，alpha 保留）—— 自定滤镜的语义 oracle。 */
QImage naiveCustom(const QImage &src, const int w[9], int scale, int off)
{
    const int W = src.width();
    const int H = src.height();
    QImage out(src.size(), src.format());
    const auto at = [&src, W, H](int x, int y) -> QRgb {
        if (x < 0 || x >= W || y < 0 || y >= H)
            return 0u;
        return qUnpremultiply(src.pixel(x, y));
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int r = 0, g = 0, b = 0;
            for (int dy = -1, k = 0; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx, ++k) {
                    const QRgb p = at(x + dx, y + dy);
                    r += w[k] * qRed(p);
                    g += w[k] * qGreen(p);
                    b += w[k] * qBlue(p);
                }
            if (scale != 0) {
                r /= scale;
                g /= scale;
                b /= scale;
            }
            r += off;
            g += off;
            b += off;
            const QRgb c = at(x, y);
            out.setPixel(x, y, qPremultiply(qRgba(qBound(0, r, 255), qBound(0, g, 255),
                                                  qBound(0, b, 255), qAlpha(c))));
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

int tMoreFilters()
{
    Reporter r;

    // —— ① 最大值 / 最小值：语义 oracle + 分块投影 + 往返 ——
    for (bool isMax : {true, false}) {
        const OpName op = isMax ? OpName::Maximum : OpName::Minimum;
        const int SW = 40, SH = 32, R = 2;
        const QImage src = PsTests::makeLayerImage(SW, SH, 29, 255);
        QImage got = src;
        FilterNode node(op);
        node.setExtremumRadius(R);
        FilterEval::applyNode(got, node);
        const QImage ref = naiveExtremum(src, R, isMax);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("%1：== 朴素极值（语义 oracle，diffs=%2）")
                            .arg(isMax ? QStringLiteral("最大值") : QStringLiteral("最小值")).arg(d));

        // 分块投影 == 整幅（面积算子最要紧的护栏）
        const int W = 256, H = 192;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 37, 220), QStringLiteral("ext"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n(op);
        n.setExtremumRadius(3);
        doc->layers().layerAt(1)->filters().append(n);
        const long long dc = PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("%1：分块投影 == 整幅合成（diffs=%2）")
                             .arg(isMax ? QStringLiteral("最大值") : QStringLiteral("最小值")).arg(dc));

        // 往返
        const QString path = PsTests::tempFilePath(isMax ? QStringLiteral("max.pslite")
                                                         : QStringLiteral("min.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            r.check(bn.op() == op && bn.extremumRadius() == 3,
                    QStringLiteral("%1：往返保住半径 3").arg(isMax ? QStringLiteral("最大值") : QStringLiteral("最小值")));
        } else {
            r.check(false, QStringLiteral("重新打开含%1的 .pslite")
                               .arg(isMax ? QStringLiteral("最大值") : QStringLiteral("最小值")));
        }
    }

    // —— ② 自定滤镜：语义 oracle + 分块投影 + 往返 ——
    {
        const int SW = 40, SH = 32;
        const QImage src = PsTests::makeLayerImage(SW, SH, 31, 255);
        // 一个「锐化 + 亮一点」的核：中心 5、四邻 -1，scale 1、offset 20
        const int w[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
        const int scale = 1, off = 20;
        QImage got = src;
        FilterNode node(OpName::CustomFilter);
        for (int i = 0; i < 9; ++i)
            node.setCustomWeight(i, w[i]);
        node.setCustomScale(scale);
        node.setCustomOffset(off);
        FilterEval::applyNode(got, node);
        const QImage ref = naiveCustom(src, w, scale, off);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0, QStringLiteral("自定：== 朴素 3×3 卷积（语义 oracle，diffs=%1）").arg(d));

        // 缩放为 0（不除）也要对
        QImage got2 = src;
        FilterNode n2(OpName::CustomFilter);
        for (int i = 0; i < 9; ++i)
            n2.setCustomWeight(i, w[i]);
        n2.setCustomScale(0);
        n2.setCustomOffset(0);
        FilterEval::applyNode(got2, n2);
        const QImage ref2 = naiveCustom(src, w, 0, 0);
        r.check(PsTests::diffCount(got2, ref2) == 0,
                QStringLiteral("自定：scale=0（不除）也正确（diffs=%1）")
                    .arg(PsTests::diffCount(got2, ref2)));

        // 分块投影 == 整幅
        const int W = 256, H = 192;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(200, 160, 41, 220), QStringLiteral("cust"));
        doc->layers().layerAt(1)->setOffset(20, 10);
        FilterNode n3(OpName::CustomFilter);
        for (int i = 0; i < 9; ++i)
            n3.setCustomWeight(i, w[i]);
        n3.setCustomScale(2);
        n3.setCustomOffset(-10);
        doc->layers().layerAt(1)->filters().append(n3);
        const long long dc = PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("自定：分块投影 == 整幅合成（diffs=%1）").arg(dc));

        // 往返
        const QString path = PsTests::tempFilePath(QStringLiteral("custom.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1 && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &bn = back->layers().layerAt(1)->filters().at(0);
            bool wok = bn.op() == OpName::CustomFilter;
            for (int i = 0; i < 9 && wok; ++i)
                wok = bn.customWeight(i) == w[i];
            r.check(wok && bn.customScale() == 2 && bn.customOffset() == -10,
                    QStringLiteral("自定：往返保住权重/缩放/位移"));
        } else {
            r.check(false, QStringLiteral("重新打开含自定滤镜的 .pslite"));
        }
    }

    return r.fails;
}
