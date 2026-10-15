/**
 * t_undoregion.cpp — T3：tile-ref 像素撤销往返 + undoBytes 量级。
 */
#include "app/historystack.h"
#include "app/undoitem.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "engine/projection.h"
#include "testutil.h"

#include <QElapsedTimer>

using namespace Ps;
using PsTests::Reporter;

int tUndoRegion()
{
    Reporter r;

    // 稀疏透明层：快照几乎无瓦片，undoBytes 才反映「触碰瓦片」量级
    constexpr int W = 512, H = 512;
    auto doc = ImageDocument::createBlank(W, H, Qt::transparent);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("有背景层"));
    if (!layer)
        return r.fails;
    layer->tiles().clearTiles();

    const QImage before = layer->materialize();
    const quint64 bytes0 = doc->history().undoBytes();

    doc->pushLayerPixelsUndo(0, QStringLiteral("画笔"));
    const QRect dirtyLocal = PaintEngine::stampDab(
        layer->tiles(), QPointF(40, 40), 14.0, QColor(200, 50, 50), PaintMode::Paint);
    r.check(!dirtyLocal.isEmpty(), QStringLiteral("画了一笔"));
    layer->invalidateContentBounds();
    layer->invalidateCompositeRaster();
    doc->markDirty(dirtyLocal.translated(layer->offsetX(), layer->offsetY()));

    const QImage afterStroke = layer->materialize();
    const quint64 bytes1 = doc->history().undoBytes();
    const quint64 delta = bytes1 - bytes0;
    // 触碰瓦片宽松上限：16 × 6144 + 64KB
    const quint64 cap = 16ull * 6144ull + 65536ull;
    r.check(delta <= cap,
            QStringLiteral("一笔 undoBytes 增量=%1 ≤ %2").arg(delta).arg(cap));
    r.info(QStringLiteral("实测一笔 undoBytes 增量=%1").arg(delta));

    doc->undo();
    r.check(PsTests::diffCount(layer->materialize(), before) == 0,
            QStringLiteral("undo 后 == 画前"));

    doc->redo();
    r.check(PsTests::diffCount(layer->materialize(), afterStroke) == 0,
            QStringLiteral("redo 后 == 画后"));

    // 再画两笔，交替 undo/redo 20 次守住对称
    doc->undo(); // 回到 before
    QImage s0 = layer->materialize();
    doc->pushLayerPixelsUndo(0, QStringLiteral("笔A"));
    PaintEngine::stampDab(layer->tiles(), QPointF(80, 90), 10.0, QColor(10, 20, 30),
                          PaintMode::Paint);
    layer->invalidateContentBounds();
    layer->invalidateCompositeRaster();
    QImage s1 = layer->materialize();
    doc->pushLayerPixelsUndo(0, QStringLiteral("笔B"));
    PaintEngine::stampDab(layer->tiles(), QPointF(110, 90), 10.0, QColor(50, 20, 30),
                          PaintMode::Paint);
    layer->invalidateContentBounds();
    layer->invalidateCompositeRaster();
    QImage s2 = layer->materialize();

    for (int k = 0; k < 20; ++k) {
        doc->undo();
        doc->undo();
        r.check(PsTests::diffCount(layer->materialize(), s0) == 0,
                QStringLiteral("交替#%1 undo×2 == s0").arg(k));
        doc->redo();
        r.check(PsTests::diffCount(layer->materialize(), s1) == 0,
                QStringLiteral("交替#%1 redo×1 == s1").arg(k));
        doc->redo();
        r.check(PsTests::diffCount(layer->materialize(), s2) == 0,
                QStringLiteral("交替#%1 redo×2 == s2").arg(k));
    }

    // —— T5 实测记账（info，不计入失败；口径见 undo-region-review R1–R3）——
    {
        auto measureOneStroke = [](ImageDocument &d, PixelUndoMode mode) -> quint64 {
            Layer *L = d.activeLayer();
            const quint64 b0 = d.history().undoBytes();
            d.pushLayerPixelsUndo(0, QStringLiteral("笔"), mode);
            PaintEngine::stampDab(L->tiles(), QPointF(40, 40), 14.0, Qt::red, PaintMode::Paint);
            return d.history().undoBytes() - b0;
        };

        // ①a 稀疏层（最有利）：快照几乎为空
        {
            auto d = ImageDocument::createBlank(2048, 2048, Qt::transparent);
            d->activeLayer()->tiles().clearTiles();
            const quint64 wl = measureOneStroke(*d, PixelUndoMode::WholeLayer);
            d->undo();
            d->activeLayer()->tiles().clearTiles();
            const quint64 tr = measureOneStroke(*d, PixelUndoMode::TileRefs);
            r.info(QStringLiteral("T5①a 2048 稀疏一笔 undoBytes：WholeLayer=%1 TileRefs=%2")
                       .arg(wl)
                       .arg(tr));
        }
        // ①b 满幅白底（最常见）：TileRefs 仍持全部瓦片引用，byteSize≈触碰瓦片深拷贝前的共享记账
        {
            auto d = ImageDocument::createBlank(2048, 2048, Qt::white);
            const quint64 wl = measureOneStroke(*d, PixelUndoMode::WholeLayer);
            d->undo();
            // 重新白底一笔（undo 已恢复）
            const quint64 b0 = d->history().undoBytes();
            d->pushLayerPixelsUndo(0, QStringLiteral("TR"), PixelUndoMode::TileRefs);
            PaintEngine::stampDab(d->activeLayer()->tiles(), QPointF(40, 40), 14.0, Qt::red,
                                  PaintMode::Paint);
            const quint64 tr = d->history().undoBytes() - b0;
            const int touched = d->activeLayer()->tiles().allocatedTileCount(); // 参考：写后总槽数
            Q_UNUSED(touched);
            r.info(QStringLiteral("T5①b 2048 满幅白底一笔 undoBytes：WholeLayer=%1 TileRefs=%2")
                       .arg(wl)
                       .arg(tr));
        }

        // ② 200 笔峰值：TileRefs vs WholeLayer（后者预期触 256MB 上限被 trim）
        auto peak200 = [](PixelUndoMode mode) -> quint64 {
            auto d = ImageDocument::createBlank(1024, 1024, Qt::transparent);
            Layer *L = d->activeLayer();
            L->tiles().clearTiles();
            quint64 peak = 0;
            for (int i = 0; i < 200; ++i) {
                d->pushLayerPixelsUndo(0, QStringLiteral("p%1").arg(i), mode);
                PaintEngine::stampDab(L->tiles(), QPointF(20 + (i % 40) * 8, 20 + (i / 40) * 8),
                                      6.0, QColor(i % 200, 40, 40), PaintMode::Paint);
                L->invalidateContentBounds();
                peak = qMax(peak, d->history().historyBytes());
            }
            return peak;
        };
        const quint64 peakTr = peak200(PixelUndoMode::TileRefs);
        const quint64 peakWl = peak200(PixelUndoMode::WholeLayer);
        r.info(QStringLiteral("T5② 1024 稀疏 200 笔 historyBytes 峰值：TileRefs=%1 (≈%2 MB) "
                              "WholeLayer=%3 (≈%4 MB，上限 256MB 会 trim)")
                   .arg(peakTr)
                   .arg(peakTr / (1024.0 * 1024.0), 0, 'f', 2)
                   .arg(peakWl)
                   .arg(peakWl / (1024.0 * 1024.0), 0, 'f', 2));

        // ③ 排空积压后再测撤销脏区重投影。
        // 注意：sync() 即使 maxMs=0 仍有块数上限（48+24），测整幅必须循环 processPending。
        auto drainProjection = [](Projection &proj) {
            while (proj.hasPendingChunks())
                proj.processPendingChunks(/*maxChunks=*/0); // 0 = 不限块数
        };
        auto measureUndoPainted = [&](PixelUndoMode mode) -> QRect {
            auto d = ImageDocument::createBlank(2048, 2048, Qt::white);
            Layer *L = d->activeLayer();
            d->pushLayerPixelsUndo(0, QStringLiteral("笔"), mode);
            const QRect loc = PaintEngine::stampDab(L->tiles(), QPointF(2000, 2000), 12.0, Qt::blue,
                                                    PaintMode::Paint);
            L->invalidateContentBounds();
            L->invalidateCompositeRaster();
            d->markDirty(loc.translated(L->offsetX(), L->offsetY()));

            Projection proj;
            proj.bind(d.get());
            proj.setPriorityRect(QRect(0, 0, 2048, 2048));
            // 首帧 sync 会 ensureBuffer（整幅无效）；必须排空，否则测到的是建缓冲积压带
            (void)proj.sync(/*maxMs=*/0);
            drainProjection(proj);
            d->clearDirtyRect();
            r.check(!proj.hasPendingChunks(),
                    QStringLiteral("T5③ 排空后无积压（mode=%1）")
                        .arg(mode == PixelUndoMode::TileRefs ? QStringLiteral("TR")
                                                             : QStringLiteral("WL")));

            d->undo(); // 只留下撤销产生的脏区
            QRect painted = proj.sync(/*maxMs=*/0);
            while (proj.hasPendingChunks())
                painted = painted.united(proj.processPendingChunks(/*maxChunks=*/0));
            return painted;
        };
        QElapsedTimer clock;
        clock.start();
        const QRect paintedTr = measureUndoPainted(PixelUndoMode::TileRefs);
        const qint64 msTr = clock.elapsed();
        clock.restart();
        const QRect paintedWl = measureUndoPainted(PixelUndoMode::WholeLayer);
        const qint64 msWl = clock.elapsed();
        r.info(QStringLiteral("T5③ 2048 白底小笔撤销后重投影（排空+循环）："
                              "TileRefs painted=%1×%2 (%3 ms)；"
                              "WholeLayer painted=%4×%5 (%6 ms)")
                   .arg(paintedTr.width())
                   .arg(paintedTr.height())
                   .arg(msTr)
                   .arg(paintedWl.width())
                   .arg(paintedWl.height())
                   .arg(msWl));
        r.check(paintedTr.width() <= 128 && paintedTr.height() <= 128,
                QStringLiteral("T5③ TileRefs painted ≤128×128（%1×%2）")
                    .arg(paintedTr.width())
                    .arg(paintedTr.height()));
        r.check(paintedWl.width() == 2048 && paintedWl.height() == 2048,
                QStringLiteral("T5③ WholeLayer painted 为整幅（%1×%2）")
                    .arg(paintedWl.width())
                    .arg(paintedWl.height()));
    }

    return r.fails;
}
