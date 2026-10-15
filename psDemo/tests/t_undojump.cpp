/**
 * t_undojump.cpp — T2：jumpToStep 任意跳转 + ChangeBatch 信号合并。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>
#include <QtGlobal>

using namespace Ps;
using PsTests::Reporter;

int tUndoJump()
{
    Reporter r;

    auto doc = ImageDocument::createBlank(256, 256, Qt::white);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("有背景层"));
    if (!layer)
        return r.fails;

    QVector<QImage> refs;
    refs.reserve(11);
    refs.push_back(layer->materialize()); // step 0 = 初始

    for (int i = 0; i < 10; ++i) {
        doc->pushLayerPixelsUndo(0, QStringLiteral("笔%1").arg(i));
        const QRect loc = PaintEngine::stampDab(
            layer->tiles(), QPointF(30 + (i % 5) * 18, 30 + (i / 5) * 18), 8.0,
            QColor(40 + i * 12, 80, 120), PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->markDirty(loc.translated(layer->offsetX(), layer->offsetY()));
        refs.push_back(layer->materialize());
    }

    HistoryStack &hist = doc->history();
    r.check(hist.stepCount() == 10 && hist.currentStep() == 10,
            QStringLiteral("建 10 笔后 stepCount=%1 currentStep=%2")
                .arg(hist.stepCount())
                .arg(hist.currentStep()));

    // 对每个 k ∈ [0,10] 跳转，与参照逐像素一致；每次 contentChanged == 1
    for (int k = 0; k <= 10; ++k) {
        int contentN = 0;
        QMetaObject::Connection conn = QObject::connect(
            doc.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        const bool jumped = hist.jumpToStep(k, *doc);
        QObject::disconnect(conn);
        if (k == 10 && hist.currentStep() == 10 && !jumped) {
            // 已在目标：允许返回 false 且不发信号
            r.check(contentN == 0, QStringLiteral("已在 k=10 时不跳不发（content=%1）").arg(contentN));
        } else {
            r.check(jumped, QStringLiteral("jumpToStep(%1) 返回 true").arg(k));
            r.check(contentN == 1,
                    QStringLiteral("jumpToStep(%1) contentChanged==1（%2）").arg(k).arg(contentN));
        }
        r.check(hist.currentStep() == k,
                QStringLiteral("游标 currentStep=%1 期望 %2").arg(hist.currentStep()).arg(k));
        r.check(PsTests::diffCount(layer->materialize(), refs[k]) == 0,
                QStringLiteral("jumpToStep(%1) materialize == 参照").arg(k));
    }

    // 来回随机跳 20 次仍一致
    quint32 seed = 0xC0FFEEu;
    for (int t = 0; t < 20; ++t) {
        seed = seed * 1664525u + 1013904223u;
        const int k = int(seed % 11u);
        int contentN = 0;
        QMetaObject::Connection conn = QObject::connect(
            doc.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        const int before = hist.currentStep();
        const bool jumped = hist.jumpToStep(k, *doc);
        QObject::disconnect(conn);
        if (k == before) {
            r.check(!jumped && contentN == 0,
                    QStringLiteral("随机#%1 同位置不跳").arg(t));
        } else {
            r.check(jumped && contentN == 1,
                    QStringLiteral("随机#%1 jump→%2 content=%3").arg(t).arg(k).arg(contentN));
        }
        r.check(PsTests::diffCount(layer->materialize(), refs[k]) == 0,
                QStringLiteral("随机#%1 k=%2 逐像素一致").arg(t).arg(k));
    }

    // 超界 clamp
    {
        hist.jumpToStep(10, *doc);
        r.check(hist.jumpToStep(-5, *doc) && hist.currentStep() == 0,
                QStringLiteral("jumpToStep(-5) clamp→0（current=%1）").arg(hist.currentStep()));
        r.check(PsTests::diffCount(layer->materialize(), refs[0]) == 0,
                QStringLiteral("clamp 到 0 后 == 初始"));
        r.check(hist.jumpToStep(999, *doc) && hist.currentStep() == 10,
                QStringLiteral("jumpToStep(999) clamp→10（current=%1）").arg(hist.currentStep()));
        r.check(PsTests::diffCount(layer->materialize(), refs[10]) == 0,
                QStringLiteral("clamp 到 10 后 == 末步"));
    }

    // stepCount==0 不崩
    {
        auto empty = ImageDocument::createBlank(32, 32, Qt::white);
        empty->history().clear();
        r.check(!empty->history().jumpToStep(0, *empty),
                QStringLiteral("stepCount==0 时 jumpToStep 返回 false"));
        r.check(empty->history().stepCount() == 0, QStringLiteral("空栈 stepCount 仍为 0"));
    }

    // stepName 基本健全
    hist.jumpToStep(10, *doc);
    r.check(hist.stepName(0) == QStringLiteral("笔0"),
            QStringLiteral("stepName(0)=%1").arg(hist.stepName(0)));
    r.check(hist.stepName(9) == QStringLiteral("笔9"),
            QStringLiteral("stepName(9)=%1").arg(hist.stepName(9)));
    r.check(hist.stepName(-1).isEmpty() && hist.stepName(10).isEmpty(),
            QStringLiteral("stepName 越界为空"));

    r.info(QStringLiteral("undojump：10 步全覆盖 + 20 次随机跳 + clamp 均 diffCount==0"));

    // T6 info：跳转 1/10/100/200 步耗时 + contentChanged 次数 + historyBytes 峰值
    {
        auto d = ImageDocument::createBlank(256, 256, Qt::white);
        Layer *L = d->activeLayer();
        for (int i = 0; i < 200; ++i) {
            d->pushLayerPixelsUndo(0, QStringLiteral("t%1").arg(i));
            const QRect loc = PaintEngine::stampDab(
                L->tiles(), QPointF(20 + (i % 16) * 12, 20 + (i / 16) * 12), 6.0,
                QColor(i % 200, 40, 80), PaintMode::Paint);
            L->invalidateContentBounds();
            L->invalidateCompositeRaster();
            d->markDirty(loc.translated(L->offsetX(), L->offsetY()));
        }
        HistoryStack &h = d->history();
        r.check(h.stepCount() == 200 && h.currentStep() == 200,
                QStringLiteral("T6 建 200 笔 stepCount=%1").arg(h.stepCount()));

        const int deltas[] = {1, 10, 100, 200};
        for (int delta : deltas) {
            h.jumpToStep(200, *d);
            int contentN = 0;
            QMetaObject::Connection conn = QObject::connect(
                d.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
            quint64 peak = h.historyBytes();
            QElapsedTimer clock;
            clock.start();
            const bool ok = h.jumpToStep(200 - delta, *d);
            const qint64 ms = clock.elapsed();
            peak = qMax(peak, h.historyBytes());
            QObject::disconnect(conn);
            r.check(ok && contentN == 1,
                    QStringLiteral("T6 Δ%1 步 jump contentChanged==1（%2）").arg(delta).arg(contentN));
            r.info(QStringLiteral("T6 跳转 Δ%1 步：%2 ms，contentChanged=%3，historyBytes=%4")
                       .arg(delta)
                       .arg(ms)
                       .arg(contentN)
                       .arg(h.historyBytes()));
            Q_UNUSED(peak);
        }

        // 专测 200 步跳转期间 historyBytes 峰值（从 200→0）
        h.jumpToStep(200, *d);
        quint64 peakBytes = h.historyBytes();
        const quint64 before = peakBytes;
        int contentN = 0;
        QMetaObject::Connection conn = QObject::connect(
            d.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        QElapsedTimer clock;
        clock.start();
        h.jumpToStep(0, *d);
        const qint64 ms200 = clock.elapsed();
        peakBytes = qMax(peakBytes, h.historyBytes());
        QObject::disconnect(conn);
        r.check(contentN == 1, QStringLiteral("T6 200→0 contentChanged==1（%1）").arg(contentN));
        r.info(QStringLiteral("T6 200→0：%1 ms，contentChanged=%2，historyBytes 跳前=%3 跳后=%4 峰值≈%5")
                   .arg(ms200)
                   .arg(contentN)
                   .arg(before)
                   .arg(h.historyBytes())
                   .arg(peakBytes));
    }

    // T6 R1：2048² 大图 Δ200（最坏口径；pending-dev ①② 须同时记小图/大图）
    {
        auto d = ImageDocument::createBlank(2048, 2048, Qt::white);
        Layer *L = d->activeLayer();
        for (int i = 0; i < 200; ++i) {
            d->pushLayerPixelsUndo(0, QStringLiteral("L%1").arg(i));
            const QRect loc = PaintEngine::stampDab(
                L->tiles(), QPointF(40 + (i % 20) * 16, 40 + (i / 20) * 16), 8.0,
                QColor(i % 200, 50, 90), PaintMode::Paint);
            L->invalidateContentBounds();
            L->invalidateCompositeRaster();
            d->markDirty(loc.translated(L->offsetX(), L->offsetY()));
        }
        HistoryStack &h = d->history();
        r.check(h.stepCount() == 200, QStringLiteral("T6R1 2048² 建 200 笔"));

        h.jumpToStep(200, *d);
        int contentN = 0;
        QMetaObject::Connection conn = QObject::connect(
            d.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        const quint64 before = h.historyBytes();
        quint64 peakBytes = before;
        QElapsedTimer clock;
        clock.start();
        const bool ok = h.jumpToStep(0, *d);
        const qint64 ms = clock.elapsed();
        peakBytes = qMax(peakBytes, h.historyBytes());
        QObject::disconnect(conn);
        r.check(ok && contentN == 1,
                QStringLiteral("T6R1 2048² 200→0 contentChanged==1（%1）").arg(contentN));
        r.info(QStringLiteral(
                   "T6R1 2048²×200 笔 200→0：%1 ms，contentChanged=%2，historyBytes 跳前=%3 跳后=%4 峰值≈%5")
                   .arg(ms)
                   .arg(contentN)
                   .arg(before)
                   .arg(h.historyBytes())
                   .arg(peakBytes));
    }

    return r.fails;
}
