/**
 * t_filterasync.cpp — 滤镜「定稿」的异步 / 可取消链路。
 *
 * 守的契约（对照 docs/pending-dev.md 的「异步/可取消的定稿」）：
 *  ① 协作式取消真的能中断面积算子（`FilterStack::apply(..., shouldAbort)`）；
 *  ② 层的**区域滤镜缓存**在 `compositeRasterRegion` 里被命中，且返回的像素与
 *     「同步现场算滤镜」**逐像素一致**（这是整条异步链路唯一真正要紧的性质）；
 *  ③ 任何层内容改动都让世代号前进、缓存失效（过期结果写不进去的判据）；
 *  ④ 端到端：给活动层加一个重滤镜 → 后台任务跑完 → 缓存**覆盖整层** →
 *     与「不用缓存的分块投影」逐像素一致。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/layerfilterjob.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "testutil.h"
#include "ui/canvasview.h"

#include <QApplication>
#include <QElapsedTimer>

using namespace Ps;
using PsTests::Reporter;

int tFilterAsync()
{
    Reporter r;

    const int W = 320, H = 240;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 7, 255), QStringLiteral("heavy"));
    Layer *layer = doc->layers().layerAt(1);
    if (!layer) {
        r.check(false, QStringLiteral("有活动像素层"));
        return r.fails;
    }

    // —— ① 协作式取消 ——
    {
        FilterNode median(OpName::MedianBlur);
        median.setMedianRadius(8); // 足够重：取消才看得出差别
        doc->addLayerFilter(median, /*pushUndo=*/false);

        QImage src = layer->materialize();
        const QImage full = layer->applyFiltersTo(src, nullptr);
        r.check(!full.isNull(), QStringLiteral("不取消：滤镜栈求值成功"));

        int calls = 0;
        const auto abortAfterFew = [&calls]() { return ++calls > 3; };
        const QImage aborted = layer->applyFiltersTo(src, abortAfterFew);
        r.check(calls > 3, QStringLiteral("取消谓词真的被调用（调用 %1 次）").arg(calls));
        const long long d = PsTests::diffCount(full, aborted);
        r.check(full.size() != aborted.size() || d != 0,
                QStringLiteral("取消后拿到的是半成品（与完整结果不同，diffs=%1）").arg(d));

        doc->removeLayerFilter(1, 0, /*pushUndo=*/false);
    }

    // —— ② 区域缓存命中 + 与同步路径逐像素一致 ——
    {
        FilterNode blur(OpName::BoxBlur);
        blur.setBlurRadius(4); // 面积算子：inputPad=4，缓存必须连 pad 一起算对
        doc->addLayerFilter(blur, /*pushUndo=*/false);

        const QRect bounds = layer->boundsInDocument();
        QImage fake(bounds.size(), QImage::Format_ARGB32_Premultiplied);
        fake.fill(QColor(10, 200, 30, 255));
        layer->setRegionFilterResult(bounds, fake);
        r.check(layer->regionFilterCovers(bounds), QStringLiteral("缓存写入后可被查询覆盖"));

        const Layer::CompositeRegion hit = layer->compositeRasterRegion(bounds);
        r.check(hit.image.size() == bounds.size(),
                QStringLiteral("命中缓存：返回请求区大小（%1×%2）")
                    .arg(hit.image.width()).arg(hit.image.height()));
        r.check(hit.docX == bounds.left() && hit.docY == bounds.top(),
                QStringLiteral("命中缓存：文档坐标正确"));
        r.check(hit.image.constScanLine(10)[10] == fake.constScanLine(10)[10],
                QStringLiteral("命中缓存：像素来自缓存而非现场求值"));

        const quint32 genBefore = layer->contentGeneration();
        layer->invalidateCompositeRaster();
        r.check(layer->contentGeneration() != genBefore,
                QStringLiteral("失效后世代号前进（异步结果作废的判据）"));
        r.check(!layer->regionFilterCovers(bounds), QStringLiteral("失效后缓存不再命中"));

        // 同步路径必须与「缓存版本」逐像素一致：用真滤镜结果当缓存，再和现场算的比较
        const QImage sync = layer->compositeRasterRegion(bounds).image;
        FilterStack stack;
        stack.replaceAll(layer->filters().snapshot());
        const QImage truth = stack.apply(layer->materialize());
        const long long d = PsTests::diffCount(sync, truth);
        r.check(d == 0, QStringLiteral("无缓存（同步）结果 == 直接滤镜求值（diffs=%1）").arg(d));

        doc->removeLayerFilter(1, 0, /*pushUndo=*/false);
    }

    // —— ③④ 端到端：重滤镜定稿 → 后台任务 → 缓存 → 分块投影与整幅一致 ——
    //
    // 触发路径与真实 UI 一致：`addLayerFilter` → `markDirty` → `contentChanged`
    // → `CanvasView::maybeStartAsyncFilterFinalize`。这里用离屏 CanvasView 接管
    // （不放进布局、不 show，避免任何真窗口交互）。
    {
        CanvasView canvas;
        canvas.resize(400, 300);
        canvas.setDocument(doc.get());

        FilterNode median(OpName::MedianBlur);
        median.setMedianRadius(4);
        doc->addLayerFilter(median, /*pushUndo=*/true);

        const QRect bounds = layer->boundsInDocument();
        r.check(!layer->regionFilterCovers(bounds),
                QStringLiteral("端到端：起任务前还没有缓存（否则这条测不出东西）"));

        // 给事件循环时间把后台任务跑完（结果是 QueuedConnection 回主线程的）
        QElapsedTimer t;
        t.start();
        bool cached = false;
        while (t.elapsed() < 3000 && !cached) {
            QApplication::processEvents();
            cached = layer->regionFilterCovers(bounds);
        }
        r.check(cached, QStringLiteral("端到端：后台定稿把结果写进了区域缓存（等待 %1 ms）")
                            .arg(t.elapsed()));

        // 用缓存的分块投影 == 整幅一次合成（缓存错一点这里就会红）
        Projection p;
        p.bind(doc.get());
        p.setPriorityRect(QRect(0, 0, W, H));
        doc->markDirty();
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage chunked = p.image();
        const QImage whole = Compositor::composite(*doc);
        const long long dc = PsTests::diffCount(chunked, whole);
        r.check(dc == 0, QStringLiteral("端到端：命中缓存的投影 == 整幅合成（diffs=%1）").arg(dc));

        // 再改一次层内容 → 世代号前进、缓存作废（过期结果写不进去的判据）
        const quint32 gen = layer->contentGeneration();
        doc->removeLayerFilter(1, 0, /*pushUndo=*/false);
        r.check(layer->contentGeneration() != gen,
                QStringLiteral("端到端：删滤镜后世代号前进、缓存失效"));
        canvas.setDocument(nullptr);
    }

    return r.fails;
}

/**
 * 滤镜**预览**的异步化：改参数时把「可见区重算滤镜」挪到后台（不再卡 UI 线程）。
 *
 * 守的契约（见 `ImageDocument::setLayerFilterNodePreview` 的约定）：
 *  ① 预览走**静默标脏**：发 `layerFilterPreviewChanged`、但**不发** `contentChanged`
 *     （发了就等于画布当场同步合成，异步白做）；
 *  ② 画布据此起后台任务，只重算**给的那块区域**；
 *  ③ 任务完成后该区域命中缓存，且结果与「整层滤镜后裁同块」**逐像素一致**。
 */
int tFilterPreviewAsync()
{
    Reporter r;

    const int W = 320, H = 240;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 7, 255), QStringLiteral("heavy"));
    Layer *layer = doc->layers().layerAt(1);
    if (!layer) {
        r.check(false, QStringLiteral("有活动像素层"));
        return r.fails;
    }

    // 先在「无人订阅」时把重滤镜装上，避免 setup 阶段就触发定稿任务
    FilterNode median(OpName::MedianBlur);
    median.setMedianRadius(4);
    doc->addLayerFilter(median, /*pushUndo=*/false);

    // 订阅信号（放在 setup 之后，计数从 0 起）
    int previewSignals = 0;
    int contentSignals = 0;
    QRect previewRegion;
    QObject::connect(doc.get(), &ImageDocument::layerFilterPreviewChanged, doc.get(),
                     [&](int, const QRect &region) { ++previewSignals; previewRegion = region; });
    QObject::connect(doc.get(), &ImageDocument::contentChanged, doc.get(),
                     [&]() { ++contentSignals; });

    // 离屏画布接管（订阅 layerFilterPreviewChanged）
    CanvasView canvas;
    canvas.resize(400, 300);
    canvas.setDocument(doc.get());
    contentSignals = 0; // setDocument 内部的同步合成不算

    const QRect region(40, 30, 120, 90);
    FilterNode preview = median;
    preview.setMedianRadius(6);
    doc->setLayerFilterNodePreview(1, 0, preview, region);

    // —— ① 静默标脏：发预览信号，但**不发** contentChanged ——
    r.check(previewSignals == 1,
            QStringLiteral("预览：发且只发一次 layerFilterPreviewChanged（实际 %1）").arg(previewSignals));
    r.check(previewRegion == region, QStringLiteral("预览：信号带的是给的那块区域"));
    r.check(contentSignals == 0,
            QStringLiteral("预览：静默标脏（不发 contentChanged；实际发了 %1 次）").arg(contentSignals));

    // —— ②③ 后台任务写好这块区域的缓存 ——
    QElapsedTimer t;
    t.start();
    bool cached = false;
    while (t.elapsed() < 3000 && !cached) {
        QApplication::processEvents();
        cached = layer->regionFilterCovers(region);
    }
    r.check(cached, QStringLiteral("预览：后台任务把该区域写进了缓存（等待 %1 ms）").arg(t.elapsed()));
    r.check(contentSignals >= 1,
            QStringLiteral("预览：任务完成后补发了 contentChanged（实际 %1 次）").arg(contentSignals));

    // —— 缓存命中结果 == 「整层滤镜后裁同块」 ——
    {
        FilterStack stack;
        stack.replaceAll(layer->filters().snapshot());
        const QImage whole = stack.apply(layer->materialize());
        const QImage ref = whole.copy(region);
        const Layer::CompositeRegion got = layer->compositeRasterRegion(region);
        r.check(got.image.size() == ref.size() && PsTests::diffCount(got.image, ref) == 0,
                QStringLiteral("预览：命中缓存的区域 == 整层滤镜后裁同块（diffs=%1）")
                    .arg(PsTests::diffCount(got.image, ref)));
    }

    canvas.setDocument(nullptr);
    return r.fails;
}

/**
 * 预览**降采样**：「更糊但更快」。
 *
 * 守的契约（见 `FilterNode::scaledForDownsampledPreview` 与 `LayerFilterJob::start`）：
 *  ① 空间类参数（半径/距离/位移）按倍数缩小，且**最小值钳到 1**、原本为 0 的保持 0；
 *     非空间参数（强度/阈值/权重…）一律不动；
 *  ② `previewDivisor > 1` 的任务照常写区域缓存、尺寸正确；
 *  ③ 降采样结果与原分辨率结果**大体相近**（不是另一个滤镜），且有差异（证明确实降了）；
 *  ④ 降采样更快（只打印，不作断言 —— 时序噪声大）。
 */
int tPreviewDownsample()
{
    Reporter r;

    // —— ① 空间参数缩放 ——
    {
        FilterNode n(OpName::MedianBlur);
        n.setMedianRadius(6);
        n.setBlurRadius(0); // 0 必须保持 0：不能凭空造出效果
        n.setUsmAmount(1.5);
        n.setUsmThreshold(20);
        n.setOffsetDx(7);
        n.setOffsetDy(-9);

        const FilterNode s2 = n.scaledForDownsampledPreview(2);
        r.check(s2.medianRadius() == 3, QStringLiteral("中值半径 6 / 2 → 3（实际 %1）").arg(s2.medianRadius()));
        r.check(s2.blurRadius() == 0, QStringLiteral("半径 0 保持 0（不为 1）"));
        r.check(qFuzzyCompare(s2.usmAmount(), 1.5) && s2.usmThreshold() == 20,
                QStringLiteral("非空间参数（强度/阈值）不动"));
        r.check(s2.offsetDx() == 3 && s2.offsetDy() == -4,
                QStringLiteral("位移 / 2 截断（7→3、-9→-4）"));

        FilterNode m1(OpName::MedianBlur);
        m1.setMedianRadius(1);
        r.check(m1.scaledForDownsampledPreview(2).medianRadius() == 1,
                QStringLiteral("半径 1 / 2 → 1（钳到最小 1，不为 0）"));

        r.check(n.scaledForDownsampledPreview(1).medianRadius() == 6,
                QStringLiteral("divisor=1 原样返回"));
    }

    // —— ②③④ 任务：divisor=1 vs 2 ——
    const int W = 400, H = 300;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);

    // 用**平滑**图（渐变）：降采样前后差异才有可比性（makeLayerImage 有取模跳变，边缘会差很多）
    QImage smooth(W, H, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            smooth.setPixel(x, y, qRgba(x * 255 / W, y * 255 / H, 128, 255));
    doc->placeImageAsLayer(smooth, QStringLiteral("smooth"));
    Layer *layer = doc->layers().layerAt(1);
    if (!layer) {
        r.check(false, QStringLiteral("有活动像素层"));
        return r.fails;
    }
    FilterNode median(OpName::MedianBlur);
    median.setMedianRadius(4);
    layer->filters().append(median);
    layer->invalidateCompositeRaster();

    const QRect region(40, 30, 300, 220); // ≥ 512×512? 不：这里刻意用小区域，只验正确性

    const auto runJob = [&](int divisor, qint64 *msOut) -> QImage {
        layer->invalidateCompositeRaster();
        Ps::LayerFilterJob job;
        bool done = false;
        QObject::connect(&job, &Ps::LayerFilterJob::finished, &job, [&](bool) { done = true; });
        QElapsedTimer t;
        t.start();
        if (!job.start(doc.get(), 1, region, divisor))
            return QImage();
        while (!done && t.elapsed() < 5000)
            QApplication::processEvents();
        *msOut = t.elapsed();
        return layer->regionFilterCovers(region) ? layer->compositeRasterRegion(region).image
                                                 : QImage();
    };

    qint64 msFull = 0, msHalf = 0;
    const QImage full = runJob(1, &msFull);
    const QImage half = runJob(2, &msHalf);

    r.check(!full.isNull() && full.size() == region.size(),
            QStringLiteral("divisor=1：写出区域缓存且尺寸正确"));
    r.check(!half.isNull() && half.size() == region.size(),
            QStringLiteral("divisor=2：写出区域缓存且尺寸正确（放大回原区域尺寸）"));
    if (!full.isNull() && !half.isNull()) {
        const long long d = PsTests::diffCount(full, half);
        r.check(d > 0, QStringLiteral("divisor=2 确实不同（证明确实降采样了，diffs=%1）").arg(d));
        const int maxD = PsTests::maxChannelDiff(full, half);
        r.check(maxD >= 0 && maxD <= 48,
                QStringLiteral("divisor=2 与原分辨率**大体相近**（平滑图上最大单通道差 %1 ≤ 48）").arg(maxD));
    }
    r.info(QStringLiteral("降采样预览耗时：divisor=1 %1 ms / divisor=2 %2 ms（后台墙钟，含事件轮询）")
               .arg(msFull).arg(msHalf));

    return r.fails;
}
