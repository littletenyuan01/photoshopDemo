/**
 * t_undobookmark.cpp — T5：命名书签跨 trim 重基 / 跳转 / 清 redo 清理。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"

#include <QObject>
#include <QVector>

using namespace Ps;
using PsTests::Reporter;

namespace {

void strokeOnce(ImageDocument &doc, int i)
{
    Layer *layer = doc.activeLayer();
    doc.pushLayerPixelsUndo(0, QStringLiteral("笔%1").arg(i));
    const QRect loc = PaintEngine::stampDab(
        layer->tiles(), QPointF(20 + (i % 8) * 10, 20 + (i / 8) * 10), 5.0,
        QColor(30 + i * 10, 50, 70), PaintMode::Paint);
    layer->invalidateContentBounds();
    layer->invalidateCompositeRaster();
    doc.markDirty(loc.translated(layer->offsetX(), layer->offsetY()));
}

} // namespace

int tUndoBookmark()
{
    Reporter r;

    auto doc = ImageDocument::createBlank(128, 128, Qt::white);
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("有背景层"));
    if (!layer)
        return r.fails;

    HistoryStack &hist = doc->history();
    QVector<QImage> refs;
    refs.push_back(layer->materialize()); // step 0

    for (int i = 0; i < 3; ++i) {
        strokeOnce(*doc, i);
        refs.push_back(layer->materialize());
    }
    r.check(hist.currentStep() == 3, QStringLiteral("做到第 3 步"));

    int bmChanged = 0;
    QObject::connect(&hist, &HistoryStack::bookmarksChanged, [&]() { ++bmChanged; });

    r.check(hist.addBookmark(3, QStringLiteral("A")), QStringLiteral("第 3 步建书签 A"));
    r.check(bmChanged >= 1, QStringLiteral("addBookmark 发 bookmarksChanged"));
    r.check(hist.bookmarkStep(QStringLiteral("A")) == 3,
            QStringLiteral("bookmarkStep(A)=%1").arg(hist.bookmarkStep(QStringLiteral("A"))));
    QString label;
    r.check(hist.bookmarkAt(3, &label) && label == QStringLiteral("A"),
            QStringLiteral("bookmarkAt(3)=A"));

    // 继续做 5 笔
    for (int i = 3; i < 8; ++i) {
        strokeOnce(*doc, i);
        refs.push_back(layer->materialize());
    }
    r.check(hist.currentStep() == 8 && hist.bookmarkStep(QStringLiteral("A")) == 3,
            QStringLiteral("续画后书签 A 仍在步 3"));

    r.check(hist.jumpToStep(hist.bookmarkStep(QStringLiteral("A")), *doc),
            QStringLiteral("jumpToStep(bookmarkStep(A))"));
    r.check(hist.currentStep() == 3, QStringLiteral("跳转后 currentStep=3"));
    r.check(PsTests::diffCount(layer->materialize(), refs[3]) == 0,
            QStringLiteral("跳到书签 A 后状态 == 第 3 步参照"));

    // 回到末尾再 trim：上限只留 2 条 undo，push 触发 trim
    hist.jumpToStep(hist.stepCount(), *doc);
    const int stepBefore = hist.bookmarkStep(QStringLiteral("A"));
    const int depthBefore = hist.undoDepth();
    int trimmed = 0;
    QObject::connect(&hist, &HistoryStack::historyTrimmed, [&](int n) { trimmed += n; });

    // 当前 undoDepth 应较大；把上限压到约 2 条触碰瓦片量级
    // 每笔约 16KB；2 条 ≈ 40KB 宽松
    hist.setByteLimit(40000);
    // setByteLimit 可能已 trim；若不够再 push
    if (trimmed == 0) {
        strokeOnce(*doc, 99);
        // push 可能再 trim
    }
    r.check(trimmed > 0 || hist.undoDepth() <= 2,
            QStringLiteral("已触发 trim 或深度≤2（trimmed=%1 depth=%2）")
                .arg(trimmed)
                .arg(hist.undoDepth()));

    const int stepAfter = hist.bookmarkStep(QStringLiteral("A"));
    if (stepAfter >= 0) {
        r.check(stepAfter == stepBefore - trimmed,
                QStringLiteral("书签重基：%1 → %2（dropped=%3）")
                    .arg(stepBefore)
                    .arg(stepAfter)
                    .arg(trimmed));
        r.check(stepAfter >= 0 && stepAfter <= hist.stepCount(),
                QStringLiteral("重基后书签步号落在 [0,stepCount]"));
    } else {
        r.check(stepBefore - trimmed < 0,
                QStringLiteral("书签已删：原步 %1 dropped=%2").arg(stepBefore).arg(trimmed));
    }
    r.check(hist.currentStep() == hist.undoDepth(),
            QStringLiteral("trim 后 currentStep 语义未破（=%1）").arg(hist.currentStep()));

    // push 清 redo：书签指向 redo 区须删除
    {
        auto d2 = ImageDocument::createBlank(64, 64, Qt::white);
        HistoryStack &h2 = d2->history();
        for (int i = 0; i < 4; ++i)
            strokeOnce(*d2, i);
        h2.addBookmark(4, QStringLiteral("Tip"));
        h2.jumpToStep(2, *d2);
        h2.addBookmark(2, QStringLiteral("Mid"));
        // 此时 step 4 书签在 redo 侧
        r.check(h2.bookmarkStep(QStringLiteral("Tip")) == 4, QStringLiteral("Tip 在步 4"));
        strokeOnce(*d2, 50); // push 清 redo
        r.check(h2.bookmarkStep(QStringLiteral("Tip")) < 0,
                QStringLiteral("push 清 redo 后 Tip 书签已删"));
        r.check(h2.bookmarkStep(QStringLiteral("Mid")) == 2,
                QStringLiteral("Mid 书签仍在步 2"));
    }

    // 上限 64（不同步号）
    {
        auto d3 = ImageDocument::createBlank(32, 32, Qt::transparent);
        d3->activeLayer()->tiles().clearTiles();
        for (int i = 0; i < 64; ++i)
            strokeOnce(*d3, i);
        HistoryStack &h3 = d3->history();
        for (int i = 0; i < 64; ++i)
            r.check(h3.addBookmark(i, QStringLiteral("X%1").arg(i)),
                    QStringLiteral("第 %1 个书签可加").arg(i + 1));
        r.check(!h3.addBookmark(64, QStringLiteral("X64")),
                QStringLiteral("第 65 个书签被拒"));
    }

    r.info(QStringLiteral("undobookmark：跳转/重基/清 redo/上限 均通过"));
    Q_UNUSED(depthBefore);
    return r.fails;
}
