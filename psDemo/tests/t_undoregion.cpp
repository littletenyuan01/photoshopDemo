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

    // —— T5 实测记账（info，不计入失败）——
    {
        // ① 同文档同笔：WholeLayer vs TileRefs 的 undoBytes 增量
        auto d2 = ImageDocument::createBlank(2048, 2048, Qt::transparent);
        Layer *L = d2->activeLayer();
        L->tiles().clearTiles();
        const quint64 b0 = d2->history().undoBytes();
        d2->pushLayerPixelsUndo(0, QStringLiteral("WL"), PixelUndoMode::WholeLayer);
        PaintEngine::stampDab(L->tiles(), QPointF(40, 40), 14.0, Qt::red, PaintMode::Paint);
        const quint64 wlDelta = d2->history().undoBytes() - b0;
        d2->undo();
        L->tiles().clearTiles();
        const quint64 b1 = d2->history().undoBytes();
        d2->pushLayerPixelsUndo(0, QStringLiteral("TR"), PixelUndoMode::TileRefs);
        PaintEngine::stampDab(L->tiles(), QPointF(40, 40), 14.0, Qt::red, PaintMode::Paint);
        const quint64 trDelta = d2->history().undoBytes() - b1;
        r.info(QStringLiteral("T5① 2048 稀疏一笔 undoBytes：WholeLayer=%1 TileRefs=%2")
                   .arg(wlDelta)
                   .arg(trDelta));

        // ② 200 笔历史峰值（TileRefs，稀疏层）
        auto d3 = ImageDocument::createBlank(1024, 1024, Qt::transparent);
        Layer *L3 = d3->activeLayer();
        L3->tiles().clearTiles();
        quint64 peak = 0;
        for (int i = 0; i < 200; ++i) {
            d3->pushLayerPixelsUndo(0, QStringLiteral("p%1").arg(i));
            PaintEngine::stampDab(L3->tiles(), QPointF(20 + (i % 40) * 8, 20 + (i / 40) * 8),
                                  6.0, QColor(i % 200, 40, 40), PaintMode::Paint);
            L3->invalidateContentBounds();
            peak = qMax(peak, d3->history().historyBytes());
        }
        r.info(QStringLiteral("T5② 200 笔 TileRefs historyBytes 峰值=%1 (≈%2 MB)")
                   .arg(peak)
                   .arg(peak / (1024.0 * 1024.0), 0, 'f', 2));

        // ③ 撤销后 Projection::sync 耗时（2048 白底小笔）
        auto d4 = ImageDocument::createBlank(2048, 2048, Qt::white);
        Layer *L4 = d4->activeLayer();
        d4->pushLayerPixelsUndo(0, QStringLiteral("笔"));
        const QRect loc = PaintEngine::stampDab(L4->tiles(), QPointF(2000, 2000), 12.0, Qt::blue,
                                                PaintMode::Paint);
        L4->invalidateContentBounds();
        L4->invalidateCompositeRaster();
        d4->markDirty(loc.translated(L4->offsetX(), L4->offsetY()));
        Projection proj;
        proj.bind(d4.get());
        proj.setPriorityRect(QRect(0, 0, 2048, 2048));
        proj.sync();
        d4->undo();
        QElapsedTimer clock;
        clock.start();
        const QRect painted = proj.sync();
        const qint64 ms = clock.elapsed();
        r.info(QStringLiteral("T5③ 2048 撤销后 sync=%1 ms，painted=%2×%3")
                   .arg(ms)
                   .arg(painted.width())
                   .arg(painted.height()));
    }

    return r.fails;
}
