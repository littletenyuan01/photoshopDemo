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
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

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

/** 全幅合成（整幅一次 compositeRegion）。 */
QImage compositeWhole(ImageDocument *doc)
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
    }

    return r.fails;
}
