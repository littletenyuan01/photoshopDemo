/**
 * t_areafilter.cpp — 面积算子（BoxBlur）+ ROI 外扩的等价性。
 *
 * 守四件事：
 *  ① 图路径：PsGraph(ImageSourceOp → PointFilterOp(BoxBlur)) 与直接 FilterEval 一致
 *     （即 requiredInputs 外扩 + process 裁回真的走通了）；
 *  ② 像素层滤镜：分块投影（每块 compositeRasterRegion 带 pad）与整幅合成一致
 *     → 跨 64×64 块边界无接缝；
 *  ③ 半径变化确实改变结果；
 *  ④ BoxBlur 不是调整层算子（护栏）；.pslite 往返保住 blurRadius。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/filtereval.h"
#include "engine/graph/pointfilterop.h"
#include "engine/graph/psgraph.h"
#include "engine/graph/sourceop.h"
#include "engine/op/filterop.h"
#include "engine/op/opname.h"

#include <algorithm>
#include <cmath>

#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QElapsedTimer>
#include <QImage>
#include <QRect>

#include <memory>

using namespace Ps;
using PsTests::Reporter;

namespace {

/**
 * 盒模糊语义 oracle：**逐像素朴素窗口**（不滑动），两趟 + 中间取整，
 * 与 applyBoxBlur 的语义一致（直通 alpha、abyss=透明、round-half-up）。
 * 慢但小图够用 —— 用来抓滑动窗口的 off-by-one / 初值错，同时验证倒数乘法等价于整数除法。
 */
QImage naiveSeparableBoxBlur(const QImage &src, int radius)
{
    const int w = src.width(), h = src.height();
    const int n = radius * 2 + 1;

    QImage straight(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            straight.setPixel(x, y, qUnpremultiply(src.pixel(x, y)));

    QImage tmp(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int xx = x + dx;
                if (xx < 0 || xx >= w)
                    continue;
                const QRgb p = straight.pixel(xx, y);
                sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
            }
            tmp.setPixel(x, y, qRgba((sr + n / 2) / n, (sg + n / 2) / n,
                                     (sb + n / 2) / n, (sa + n / 2) / n));
        }
    }

    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h)
                    continue;
                const QRgb p = tmp.pixel(x, yy);
                sr += qRed(p); sg += qGreen(p); sb += qBlue(p); sa += qAlpha(p);
            }
            out.setPixel(x, y, qRgba((sr + n / 2) / n, (sg + n / 2) / n,
                                     (sb + n / 2) / n, (sa + n / 2) / n));
        }
    }

    QImage premul(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            premul.setPixel(x, y, qPremultiply(out.pixel(x, y)));
    return premul;
}

/**
 * 朴素 3×3 Sobel（逐像素、abyss=透明）—— 查找边缘算子的**语义 oracle**。
 * 与算子同一 alpha 口径：采样前反预乘、结果再预乘。
 * （这层 oracle 正是抓出「FindEdges 漏了直通↔预乘转换」的那面镜子，见缺陷台账 #6）
 */
QImage naiveSobel(const QImage &src)
{
    const int w = src.width();
    const int h = src.height();
    // 用源图格式（引擎缓冲是 ARGB32_Premultiplied；diffCount 对格式不一致会给 -1 哨兵）
    QImage out(src.size(), src.format());
    const auto at = [&src, w, h](int x, int y) -> QRgb {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return 0u; // abyss = 透明
        return qUnpremultiply(src.pixel(x, y));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int gx[3] = {0, 0, 0};
            int gy[3] = {0, 0, 0};
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const QRgb p = at(x + dx, y + dy);
                    const int kx = dx * (dy == 0 ? 2 : 1);
                    const int ky = dy * (dx == 0 ? 2 : 1);
                    gx[0] += kx * qRed(p); gx[1] += kx * qGreen(p); gx[2] += kx * qBlue(p);
                    gy[0] += ky * qRed(p); gy[1] += ky * qGreen(p); gy[2] += ky * qBlue(p);
                }
            }
            const auto mag = [](int a, int b) -> int {
                return qBound(0, int(std::sqrt(double(a) * a + double(b) * b) + 0.5), 255);
            };
            out.setPixel(x, y, qPremultiply(qRgba(mag(gx[0], gy[0]), mag(gx[1], gy[1]),
                                                  mag(gx[2], gy[2]), qAlpha(at(x, y)))));
        }
    }
    return out;
}

/**
 * 朴素**精确**高斯（可分离、直通空间、越界当透明、核归一化到 1）——
 * 高斯模糊算子的**精度参照**。只用来比内部区域（边缘受 abyss 影响，不是逼近误差）。
 */
QImage naiveGaussian(const QImage &src, double sigma)
{
    const int w = src.width();
    const int h = src.height();
    const int r = int(std::ceil(3.0 * sigma));
    QVector<double> k(2 * r + 1);
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) {
        k[i + r] = std::exp(-(double(i) * i) / (2.0 * sigma * sigma));
        sum += k[i + r];
    }
    for (double &v : k)
        v /= sum;

    const auto chan = [](const QImage &img, int x, int y, int c) -> double {
        const QRgb p = qUnpremultiply(img.pixel(x, y));
        return (c == 0) ? qRed(p) : ((c == 1) ? qGreen(p) : ((c == 2) ? qBlue(p) : qAlpha(p)));
    };

    QVector<double> mid(size_t(w) * size_t(h) * 4, 0.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c) {
                double acc = 0.0;
                for (int i = -r; i <= r; ++i) {
                    const int sx = x + i;
                    if (sx < 0 || sx >= w)
                        continue; // abyss = 透明
                    acc += k[i + r] * chan(src, sx, y, c);
                }
                mid[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = acc;
            }
        }
    }

    QImage out(src.size(), src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int v[4] = {0, 0, 0, 0};
            for (int c = 0; c < 4; ++c) {
                double acc = 0.0;
                for (int i = -r; i <= r; ++i) {
                    const int sy = y + i;
                    if (sy < 0 || sy >= h)
                        continue;
                    acc += k[i + r] * mid[(size_t(sy) * size_t(w) + size_t(x)) * 4 + size_t(c)];
                }
                v[c] = qBound(0, int(acc + 0.5), 255);
            }
            // 与算子同口径：alpha 也参与滤波，RGB 用滤波后的直通值再预乘
            out.setPixel(x, y, qPremultiply(qRgba(v[0], v[1], v[2], v[3])));
        }
    }
    return out;
}

/**
 * 朴素中值（逐像素取窗口 → `std::sort` → 取正中）——中值算子的**语义 oracle**。
 * 刻意用「排序」而不是直方图：与实现（滑窗直方图）在方法上互相独立，否则两处会一起错。
 */
QImage naiveMedian(const QImage &src, int r)
{
    const int w = src.width();
    const int h = src.height();
    QImage out(src.size(), src.format());
    const auto ch = [&src, w, h](int x, int y, int c) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return 0; // abyss = 透明
        const QRgb p = qUnpremultiply(src.pixel(x, y));
        return (c == 0) ? qRed(p) : ((c == 1) ? qGreen(p) : ((c == 2) ? qBlue(p) : qAlpha(p)));
    };
    QVector<int> samples;
    const int n = (2 * r + 1) * (2 * r + 1);
    samples.resize(n);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int v[4] = {0, 0, 0, 0};
            for (int c = 0; c < 4; ++c) {
                int i = 0;
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                        samples[i++] = ch(x + dx, y + dy, c);
                std::sort(samples.begin(), samples.end());
                v[c] = samples[n / 2]; // n 恒为奇数 → 正中间就是中值
            }
            out.setPixel(x, y, qPremultiply(qRgba(v[0], v[1], v[2], v[3])));
        }
    }
    return out;
}

/** 全幅合成（整幅一次 compositeRegion）。 */QImage compositeWhole(ImageDocument *doc)
{
    return Compositor::composite(*doc);
}

/** 分块投影全排空后的图像。 */
QImage compositeChunked(ImageDocument *doc)
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

int tAreaFilter()
{
    Reporter r;

    // —— ① 图路径：requiredInputs 外扩 + 裁回 ——
    {
        const QImage src = PsTests::makeLayerImage(64, 64, 5, 200);
        FilterNode node(OpName::BoxBlur);
        node.setBlurRadius(2);

        PsGraph g;
        const GraphNode::Id srcId = g.addNode(std::make_shared<ImageSourceOp>(src));
        const GraphNode::Id blurId = g.addNode(std::make_shared<PointFilterOp>(node));
        g.connect(srcId, blurId, 0);

        const QRect out(8, 8, 32, 32); // 内部区域，外扩 2 仍在源内 → 无裁剪
        const QImage got = g.evaluate(blurId, out);

        QImage refImg = src.copy(6, 6, 36, 36);
        FilterEval::applyNode(refImg, node);
        refImg = refImg.copy(2, 2, 32, 32);

        const long long d = PsTests::diffCount(got, refImg);
        r.check(d == 0, QStringLiteral("图路径 BoxBlur 与直接 FilterEval 一致（diffs=%1）").arg(d));
    }

    // —— ② 像素层滤镜：分块(带 pad) == 整幅 ——
    {
        const int W = 256, H = 256;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(180, 150, 3, 220), QStringLiteral("blurL"));
        Layer *layer = doc->layers().layerAt(1);
        layer->setOffset(30, 40);
        FilterNode node(OpName::BoxBlur);
        node.setBlurRadius(3);
        layer->filters().append(node);

        const QImage whole = compositeWhole(doc.get());
        const QImage chunked = compositeChunked(doc.get());
        const long long d = PsTests::diffCount(whole, chunked);
        r.check(d == 0,
                QStringLiteral("像素层 BoxBlur：分块投影 == 整幅合成，无跨块接缝（diffs=%1）").arg(d));
    }

    // —— ③ 半径变化改变结果 ——
    {
        const int W = 128, H = 128;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 7, 255), QStringLiteral("blurL"));
        Layer *layer = doc->layers().layerAt(1);
        FilterNode n0(OpName::BoxBlur);
        n0.setBlurRadius(0);
        layer->filters().append(n0);
        const QImage r0 = compositeWhole(doc.get());

        layer->filters().at(0).setBlurRadius(4);
        const QImage r4 = compositeWhole(doc.get());
        const long long d = PsTests::diffCount(r0, r4);
        r.check(d > 0, QStringLiteral("半径 0 vs 4 结果不同（diffs=%1）").arg(d));
    }

    // —— ④ 护栏 + 往返 ——
    {
        r.check(!isAdjustmentOp(OpName::BoxBlur),
                QStringLiteral("BoxBlur 不是调整层算子（护栏）"));

        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(64, 64, 9, 255), QStringLiteral("blurL"));
        Layer *layer = doc->layers().layerAt(1);
        FilterNode node(OpName::BoxBlur);
        node.setBlurRadius(5);
        layer->filters().append(node);

        const QString path = PsTests::tempFilePath(QStringLiteral("areafilter.pslite"));
        QString err;
        bool saved = ProjectIo::save(*doc, path, &err);
        r.check(saved, QStringLiteral("保存 .pslite"));
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        r.check(back != nullptr, QStringLiteral("重新打开 .pslite"));
        if (back && back->layers().count() > 1) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::BoxBlur && n.blurRadius() == 5,
                    QStringLiteral("往返保住 BoxBlur 与 radius=5"));
        }
    }

    // —— ⑤ 语义 oracle：滑动窗口 == 逐像素朴素窗口 ——
    // （只验「区域==整幅」会漏掉「两处都错得一样」；这里用独立实现做真值）
    {
        const int W = 32, H = 24, R = 2;
        const QImage src = PsTests::makeLayerImage(W, H, 11, 190);
        FilterNode node(OpName::BoxBlur);
        node.setBlurRadius(R);

        QImage got = src;
        FilterEval::applyNode(got, node);
        const QImage ref = naiveSeparableBoxBlur(src, R);

        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0,
                QStringLiteral("盒模糊：滑动窗口 == 朴素窗口（语义 oracle，diffs=%1）").arg(d));
    }

    // —— ⑥ FilterOp 三段式与每线程常驻实例协议 ——
    // （每线程一份：并行分块会多线程并发调用同一算子，共享可变状态是并行崩溃的根源）
    {
        FilterOp *a = filterOpInstance(OpName::BoxBlur);
        FilterOp *b = filterOpInstance(OpName::BoxBlur);
        r.check(a != nullptr, QStringLiteral("BoxBlur 已注册为 FilterOp（走三段式）"));
        r.check(a == b, QStringLiteral("同一线程内是常驻实例（临时缓冲跨调用复用）"));
        r.check(filterOpInstance(OpName::UnsharpMask) != nullptr,
                QStringLiteral("USM 已注册为 FilterOp"));
        r.check(filterOpInstance(OpName::FindEdges) != nullptr,
                QStringLiteral("查找边缘已注册为 FilterOp"));
    }

    // —— ⑦ USM 锐化：分块(带 pad) == 整幅；强度 0 = 无操作；强度改变结果 ——
    {
        const int W = 256, H = 256;
        auto makeDoc = [&]() {
            auto d = ImageDocument::createBlank(W, H, Qt::white);
            d->placeImageAsLayer(PsTests::makeLayerImage(180, 150, 11, 220),
                                 QStringLiteral("usmL"));
            d->layers().layerAt(1)->setOffset(30, 40);
            return d;
        };
        auto docPlain = makeDoc();
        const QImage plain = compositeWhole(docPlain.get());

        auto doc = makeDoc();
        Layer *layer = doc->layers().layerAt(1);
        FilterNode node(OpName::UnsharpMask);
        node.setUsmRadius(3);
        node.setUsmAmount(1.5);
        node.setUsmThreshold(0);
        layer->filters().append(node);

        const QImage whole = compositeWhole(doc.get());
        const QImage chunked = compositeChunked(doc.get());
        const long long d = PsTests::diffCount(whole, chunked);
        r.check(d == 0,
                QStringLiteral("像素层 USM：分块投影 == 整幅合成，无跨块接缝（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(plain, whole) > 0, QStringLiteral("USM 强度 1.5 改变了画面"));

        layer->filters().at(0).setUsmAmount(0.0);
        const QImage noOp = compositeWhole(doc.get());
        const long long d0 = PsTests::diffCount(plain, noOp);
        r.check(d0 == 0, QStringLiteral("USM 强度 0 == 未加滤镜（diffs=%1）").arg(d0));

        // 往返：USM 三个新字段都要能存回来（projectio 在固定 512B 块尾部追加）
        layer->filters().at(0).setUsmRadius(3);
        layer->filters().at(0).setUsmAmount(2.0);
        layer->filters().at(0).setUsmThreshold(7);
        const QString path = PsTests::tempFilePath(QStringLiteral("usm.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        r.check(saved, QStringLiteral("保存含 USM 的 .pslite"));
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        r.check(back != nullptr, QStringLiteral("重新打开含 USM 的 .pslite"));
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::UnsharpMask && n.usmRadius() == 3
                        && qAbs(n.usmAmount() - 2.0) < 1e-6 && n.usmThreshold() == 7,
                    QStringLiteral("往返保住 USM 半径/强度/阈值"));
        }
    }

    // —— ⑧ 查找边缘：语义 oracle（朴素 3×3 Sobel）+ 分块 == 整幅（pad=1 漏了就会有接缝）——
    {
        const QImage src = PsTests::makeLayerImage(48, 40, 21, 255);
        QImage got = src;
        FilterNode node(OpName::FindEdges);
        FilterEval::applyNode(got, node);
        const QImage ref = naiveSobel(src);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0,
                QStringLiteral("查找边缘：与朴素 3×3 Sobel 逐像素一致（diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("查找边缘确实改变了画面"));

        const int W = 256, H = 256;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(180, 150, 13, 220),
                               QStringLiteral("edgeL"));
        doc->layers().layerAt(1)->setOffset(30, 40);
        doc->layers().layerAt(1)->filters().append(FilterNode(OpName::FindEdges));
        const QImage whole = compositeWhole(doc.get());
        const QImage chunked = compositeChunked(doc.get());
        const long long dc = PsTests::diffCount(whole, chunked);
        r.check(dc == 0,
                QStringLiteral("像素层查找边缘：分块投影 == 整幅合成，无跨块接缝（diffs=%1）").arg(dc));
    }

    // —— ⑨ 高斯模糊：分块(带 pad) == 整幅；与精确高斯的量化误差；往返 ——
    {
        const int W = 256, H = 256;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(180, 150, 17, 220),
                               QStringLiteral("gaussL"));
        doc->layers().layerAt(1)->setOffset(30, 40);
        FilterNode node(OpName::GaussianBlur);
        node.setGaussRadius(8);
        doc->layers().layerAt(1)->filters().append(node);

        const QImage whole = compositeWhole(doc.get());
        const QImage chunked = compositeChunked(doc.get());
        const long long d = PsTests::diffCount(whole, chunked);
        r.check(d == 0,
                QStringLiteral("像素层高斯：分块投影 == 整幅合成，无跨块接缝（diffs=%1）").arg(d));

        // 与「朴素精确高斯」的误差（σ = 半径/3，与算子同一口径）
        const int SW = 64, SH = 64, R = 9;
        const QImage src = PsTests::makeLayerImage(SW, SH, 23, 255);
        QImage got = src;
        FilterNode gnode(OpName::GaussianBlur);
        gnode.setGaussRadius(R);
        FilterEval::applyNode(got, gnode);
        const QImage ref = naiveGaussian(src, R / 3.0);
        const QRect inner(2 * R, 2 * R, SW - 4 * R, SH - 4 * R);
        const int worst = PsTests::maxChannelDiff(got.copy(inner), ref.copy(inner));
        r.info(QStringLiteral("三次盒模糊 vs 精确高斯：内部最大单通道误差 %1/255").arg(worst));
        r.check(worst >= 0 && worst <= 8,
                QStringLiteral("高斯：三次盒模糊逼近精确高斯，内部误差 ≤ 8/255（实测 %1）").arg(worst));

        // 往返：gaussRadius 要能存回来
        const QString path = PsTests::tempFilePath(QStringLiteral("gauss.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::GaussianBlur && n.gaussRadius() == 8,
                    QStringLiteral("往返保住高斯半径 8"));
        } else {
            r.check(false, QStringLiteral("重新打开含高斯的 .pslite"));
        }
    }

    // —— ⑩ 中值：语义 oracle（朴素排序中值）+ 分块 == 整幅 + 往返 ——
    {
        const int SW = 40, SH = 32, R = 2;
        const QImage src = PsTests::makeLayerImage(SW, SH, 29, 255);
        QImage got = src;
        FilterNode node(OpName::MedianBlur);
        node.setMedianRadius(R);
        FilterEval::applyNode(got, node);
        const QImage ref = naiveMedian(src, R);
        const long long d = PsTests::diffCount(got, ref);
        r.check(d == 0,
                QStringLiteral("中值：滑窗直方图 == 朴素排序中值（语义 oracle，diffs=%1）").arg(d));
        r.check(PsTests::diffCount(src, got) > 0, QStringLiteral("中值确实改变了画面"));

        const int W = 256, H = 256;
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(180, 150, 37, 220),
                               QStringLiteral("medianL"));
        doc->layers().layerAt(1)->setOffset(30, 40);
        FilterNode mnode(OpName::MedianBlur);
        mnode.setMedianRadius(3);
        doc->layers().layerAt(1)->filters().append(mnode);

        const QImage whole = compositeWhole(doc.get());
        const QImage chunked = compositeChunked(doc.get());
        const long long dc = PsTests::diffCount(whole, chunked);
        r.check(dc == 0,
                QStringLiteral("像素层中值：分块投影 == 整幅合成，无跨块接缝（diffs=%1）").arg(dc));

        // 成本实测（不是估计）：中值是目前最重的面积算子，给个可核对的数字
        {
            const int BW = 512, BH = 512;
            QImage big = PsTests::makeLayerImage(BW, BH, 41, 255);
            FilterNode bnode(OpName::MedianBlur);
            bnode.setMedianRadius(3);
            QElapsedTimer t;
            t.start();
            FilterEval::applyNode(big, bnode);
            const qint64 ms = t.elapsed();
            const double nsPerPx = double(ms) * 1e6 / double(BW) * double(BH) / double(BW) / double(BH);
            r.info(QStringLiteral("中值 r=3：%1x%2 用 %3 ms（约 %4 ns/像素；本机单次噪声可达 ±50%）")
                       .arg(BW).arg(BH).arg(ms).arg(nsPerPx, 0, 'f', 1));
        }

        // 往返：medianRadius 要能存回来
        const QString path = PsTests::tempFilePath(QStringLiteral("median.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back && back->layers().count() > 1
            && back->layers().layerAt(1)->filters().count() > 0) {
            const FilterNode &n = back->layers().layerAt(1)->filters().at(0);
            r.check(n.op() == OpName::MedianBlur && n.medianRadius() == 3,
                    QStringLiteral("往返保住中值半径 3"));
        } else {
            r.check(false, QStringLiteral("重新打开含中值的 .pslite"));
        }
    }

    return r.fails;
}
