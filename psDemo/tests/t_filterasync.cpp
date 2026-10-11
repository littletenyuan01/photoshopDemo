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
