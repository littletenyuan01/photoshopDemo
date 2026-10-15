/**
 * t_undojump.cpp — T2：jumpToStep 任意跳转 + ChangeBatch 信号合并。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

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
    return r.fails;
}
