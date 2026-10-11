/**
 * layerfilterjob.cpp — layerfilterjob.h 实现（engine 层）。
 *
 * 线程模型与取消协议见头文件。三处实现细节值得单独说明：
 *  1. 快照在 `start()` 里做（主线程），后台只读那张独立 `QImage`；
 *  2. 后台**按横向条带并行**（理由见下），条带之间写 `result` 的不相交区域；
 *  3. 结果回主线程用 `QMetaObject::invokeMethod(this, …, Qt::QueuedConnection)`，
 *     与 `ui/canvasview.cpp` 的调整层预览同一套路（对象不跨线程移动）。
 */
#include "layerfilterjob.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"
#include "engine/op/tilepatch.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrent>

#include <cstring>
#include <utility>

namespace Ps {

LayerFilterJob::LayerFilterJob(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
}

LayerFilterJob::~LayerFilterJob()
{
    // 析构时先取消：后台任务不持有 this，所以它不会回调到已析构的对象；
    // 置旗标只是让谓词尽快为 true，少算一点是一点。
    if (m_cancel)
        m_cancel->store(true);
}

bool LayerFilterJob::start(ImageDocument *doc, int layerIndex, const QRect &want,
                           int previewDivisor)
{
    if (!doc || layerIndex < 0 || want.isEmpty())
        return false;

    Layer *layer = doc->layers().layerAt(layerIndex);
    if (!layer || layer->isAdjustmentLayer() || !layer->isVisible())
        return false;
    // 有启用样式时 compositeRasterRegion 走整层样式缓存、不吃区域滤镜缓存，
    // 故异步定稿对它没有意义（见 layer.cpp 的注释）。
    if (layer->styles().hasEnabled() || !layer->filters().hasEnabled())
        return false;

    // 接替在途任务（若有）：先让旧任务尽早上路，再接管「谁说话算数」。
    // 【为什么允许接替】见头文件：同一改动会连续触发两次 contentChanged，
    // 若第二次拒启，就会出现「第一个被取消、第二个没起来」→ 一次都不写缓存。
    cancel();
    const quint32 runId = ++m_runId;

    // —— 主线程：算好「请求区 + pad」的层内矩形，抽出独立快照 ——
    const int pad = layer->filters().totalPad();
    const QRect layerRect(0, 0, layer->width(), layer->height());
    const QRect wantLocal =
        want.translated(-layer->offsetX(), -layer->offsetY()).intersected(layerRect);
    if (wantLocal.isEmpty())
        return false;
    const QRect srcLocal = wantLocal.adjusted(-pad, -pad, pad, pad).intersected(layerRect);
    const QImage snap = TilePatch::extract(layer->tiles(), srcLocal);
    if (snap.isNull())
        return false;

    FilterStack stack;
    stack.replaceAll(layer->filters().snapshot());
    const QPoint srcOrigin(srcLocal.left(), srcLocal.top());
    const QRect outRect(wantLocal.left() + layer->offsetX(), wantLocal.top() + layer->offsetY(),
                        wantLocal.width(), wantLocal.height());
    // 记账：结果只对「此刻的层内容」有效。中间任何改动都会 ++contentGeneration → 结果作废。
    const quint32 generation = layer->contentGeneration();

    // 本代任务用**自己的**取消旗标：接替时旧旗标置 true，新任务从干净状态开始
    // （否则把旧旗标共享给新任务，新任务一开始就是「已取消」）。
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    m_cancel = cancelFlag;

    m_running.store(true);
    m_progress.store(0.0);

    QPointer<ImageDocument> docPtr(doc);
    const int previewDiv = qMax(1, previewDivisor);

    auto future = QtConcurrent::run([this, docPtr, layerIndex, stack, snap, srcOrigin, outRect,
                                     pad, generation, runId, cancelFlag, previewDiv]() {
        // 【线程安全】只碰 snap（独立快照）与 cancelFlag（原子）——**不读** doc/layer/瓦片。
        const auto shouldAbort = [cancelFlag]() { return cancelFlag->load(); };

        const QSize outSize = outRect.size();
        const int outLocalLeft = outRect.left() - srcOrigin.x();
        const int outLocalTop = outRect.top() - srcOrigin.y();
        QImage result(outSize, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);

        // 【按横向条带并行】整层求值原本发生在合成路径里、是**并行**的（投影按 64×64 块
        // `blockingMap`）。若后台改成单线程跑整层，反倒比原来更慢 —— 实测 1600×1200
        // 中值 r=3：并行分块 135 ms vs 单线程整层 386 ms，那就成了「UI 不冻了但更慢」。
        // 所以这里自己也切条带：每条带取「输出行 ± pad」的输入、求值后裁回本带输出行，
        // 条带之间**互不相交**（与 `Compositor::blendLayerRangeParallel` 同一套路）。
        // pad 是硬约束：条带太薄时 pad 会把相邻条带的输入吃光，故按 pad 反推条带数上限。
        const int maxStripsByPad = (pad > 0) ? qMax(1, outSize.height() / (4 * pad)) : 64;
        const int threads = qMax(1, QThreadPool::globalInstance()->maxThreadCount());
        const int strips =
            qBound(1, qMin(threads, qMax(1, outSize.height() / 64)), maxStripsByPad);

        QVector<QRect> outBands;
        outBands.reserve(strips);
        {
            const int base = outSize.height() / strips;
            const int extra = outSize.height() % strips;
            int top = 0;
            for (int i = 0; i < strips; ++i) {
                const int bandH = base + (i < extra ? 1 : 0);
                outBands.append(QRect(0, top, outSize.width(), bandH));
                top += bandH;
            }
        }

        // 单条带：裁输入（本带输出区 ± pad）→ 求值 → 裁回本带 → 写入 result 对应行。
        //
        // 【几何：别漏掉 outLocalLeft/Top】`snap` 是「请求区外扩 pad」抽出来的图，所以
        // `outRect` 在 snap 本地坐标里的原点是 `(outLocalLeft, outLocalTop)`，而 pad 是**真实存在**的
        // （不像「want = 整层」时那样被 intersect 裁剪吸收掉）。
        // 定稿路径 want=整层 ⇒ outLocal 恒为 0，公式退化后仍对；预览路径 want=子区域 ⇒ 必须带上。
        // 曾漏掉这两项：条带取到的是「未外扩」的列、裁回也少偏一圈 → 预览区域整块像素全错
        // （实测 diffs=10800 = 恰好区域面积），而定稿测试照样绿。
        const auto bandWorker = [&](const QRect &band) {
            if (cancelFlag->load())
                return;
            const QRect srcBand = band.translated(outLocalLeft, outLocalTop)
                                      .adjusted(-pad, -pad, pad, pad)
                                      .intersected(QRect(0, 0, snap.width(), snap.height()));
            if (srcBand.isEmpty())
                return;
            QImage work = snap.copy(srcBand);
            QImage filtered = stack.apply(work, shouldAbort);
            if (cancelFlag->load() || filtered.isNull())
                return;
            const QRect keep(outLocalLeft + band.left() - srcBand.left(),
                             outLocalTop + band.top() - srcBand.top(), band.width(),
                             band.height());
            if (keep.left() < 0 || keep.top() < 0 || keep.right() >= filtered.width()
                || keep.bottom() >= filtered.height())
                return; // 几何不成立（理论上不会）：宁可少一块，也不要贴错位置
            // 【不能用 QPainter】多个条带会**并发**写同一张 result：QPainter 对同一 paint
            // device 不能同时绘制（实测 `QPainter::begin: A paint device can only be painted
            // by one painter at a time`，条带结果被丢掉、与整层差出 24 万像素）。
            // 这正是本项目在分块并行合成里踩过的同一个坑，修法也一样：**逐行 memcpy**。
            const int bytes = band.width() * int(sizeof(QRgb));
            for (int row = 0; row < band.height(); ++row) {
                const QRgb *src =
                    reinterpret_cast<const QRgb *>(filtered.constScanLine(keep.top() + row))
                    + keep.left();
                QRgb *dst = reinterpret_cast<QRgb *>(result.scanLine(band.top() + row))
                            + band.left();
                std::memcpy(dst, src, size_t(bytes));
            }
        };

        if (!cancelFlag->load()) {
            if (previewDiv <= 1) {
                if (strips <= 1)
                    bandWorker(outBands.first());
                else
                    QtConcurrent::blockingMap(outBands, bandWorker);
            } else {
                // 【降采样预览】「更糊但更快」：快照缩小 previewDiv 倍、**空间参数同步缩小**
                // （否则效果会强 previewDiv 倍，预览与定稿差得离谱）、求值后再放大回 outSize。
                // 求值量按 previewDiv² 下降 —— 拖重滤镜参数时预览延迟因此明显变短。
                // 这里**不切条带**：图已经小了，单线程足够；少一处几何换算就少一个易错点。
                const QSize smallSize(qMax(1, snap.width() / previewDiv),
                                      qMax(1, snap.height() / previewDiv));
                const QImage smallSnap =
                    snap.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

                FilterStack smallStack;
                QVector<FilterNode> nodes = stack.snapshot();
                for (FilterNode &n : nodes)
                    n = n.scaledForDownsampledPreview(previewDiv);
                smallStack.replaceAll(nodes);

                const QImage smallFiltered = smallStack.apply(smallSnap, shouldAbort);
                if (!cancelFlag->load() && !smallFiltered.isNull()) {
                    // want 在 snap 本地坐标下的位置 → 缩到 small 域（再 ∩ 一次，吃掉整除误差）
                    const QRect wantSmall =
                        QRect(outLocalLeft / previewDiv, outLocalTop / previewDiv,
                              qMax(1, outSize.width() / previewDiv),
                              qMax(1, outSize.height() / previewDiv))
                            .intersected(smallFiltered.rect());
                    if (!wantSmall.isEmpty()) {
                        result = smallFiltered.copy(wantSmall)
                                     .scaled(outSize, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
                    }
                }
            }
        }

        m_progress.store(0.5);

        // 无论成败/取消都回主线程收尾：置忙闲、清进度、发 finished。
        // 这样「取消」不会把 m_running 永远留在 true（否则该层再也发不起异步任务）。
        QMetaObject::invokeMethod(
            this,
            [this, docPtr, layerIndex, outRect, generation, runId, result, cancelFlag]() {
                // 【只认自己那一代】被接替（m_runId 已前进）→ 什么都不做
                if (m_runId.load() != runId)
                    return;
                m_progress.store(1.0);
                bool wrote = false;
                // 主线程复查：未取消、文档还在、层还在且下标未变、**层内容未变**
                if (!cancelFlag->load() && docPtr && !result.isNull()
                    && layerIndex < docPtr->layers().count()) {
                    if (Layer *layer = docPtr->layers().layerAt(layerIndex)) {
                        if (layer->contentGeneration() == generation) {
                            // 【顺序是正确性的一部分】「静默标脏 → 写缓存 → 自通知」三步：
                            //  1) `markDirtyQuiet` 会失效活动层缓存（含区域滤镜缓存），
                            //     所以必须在写缓存**之前**；用静默版是为了不让它同步发
                            //     contentChanged —— 那会让画布当场合成一次，而此刻缓存还没写，
                            //     等于把刚挪到后台的重算又在 UI 线程上跑一遍（白干）。
                            //  2) 反过来「先写缓存后标脏」更不行：那会把自己刚写的清掉。
                            docPtr->markDirtyQuiet(outRect);
                            layer->setRegionFilterResult(outRect, result);
                            // 一次性通知：缩略图/面板走 pixelsChanged，画布走 contentChanged
                            //（此刻缓存已就位 → 投影命中缓存，只做取子图 + 混合）。
                            emit docPtr->pixelsChanged(outRect);
                            emit docPtr->contentChanged();
                            wrote = true;
                        }
                    }
                }
                m_running.store(false);
                m_runId.store(0); // 本代结束
                emit finished(wrote);
            },
            Qt::QueuedConnection);
    });
    Q_UNUSED(future);
    return true;
}

void LayerFilterJob::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

} // namespace Ps
