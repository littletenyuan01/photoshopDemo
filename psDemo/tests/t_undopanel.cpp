/**
 * t_undopanel.cpp — T4：HistoryPanel 行数 / 点击跳转 / trim 后重建。
 */
#include "app/appsession.h"
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "engine/painttypes.h"
#include "testutil.h"
#include "ui/historypanel.h"

#include <QListWidget>
#include <QVector>
#include <memory>

using namespace Ps;
using PsTests::Reporter;

int tUndoPanel()
{
    Reporter r;

    AppSession session;
    auto owned = ImageDocument::createBlank(128, 128, Qt::white);
    ImageDocument *doc = owned.get();
    session.setDocument(std::move(owned));
    Layer *layer = doc->activeLayer();
    r.check(layer != nullptr, QStringLiteral("有背景层"));
    if (!layer)
        return r.fails;

    HistoryPanel panel;
    panel.setSession(&session);

    auto *list = panel.findChild<QListWidget *>(QStringLiteral("itemList"));
    r.check(list != nullptr, QStringLiteral("有 itemList"));
    if (!list)
        return r.fails;

    // 初始：仅「初始状态」一行
    r.check(list->count() == 1 && doc->history().stepCount() == 0,
            QStringLiteral("初始行数=%1 stepCount=%2").arg(list->count()).arg(doc->history().stepCount()));

    QVector<QImage> refs;
    refs.push_back(layer->materialize());
    for (int i = 0; i < 5; ++i) {
        doc->pushLayerPixelsUndo(0, QStringLiteral("笔%1").arg(i));
        const QRect loc = PaintEngine::stampDab(
            layer->tiles(), QPointF(20 + i * 12, 20 + i * 8), 6.0, QColor(50 + i * 30, 40, 40),
            PaintMode::Paint);
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
        doc->markDirty(loc.translated(layer->offsetX(), layer->offsetY()));
        refs.push_back(layer->materialize());
    }

    r.check(list->count() == doc->history().stepCount() + 1,
            QStringLiteral("5 笔后 itemList->count()=%1 == stepCount+1=%2")
                .arg(list->count())
                .arg(doc->history().stepCount() + 1));
    r.check(list->count() == 6, QStringLiteral("期望 6 行（初始+5）实际 %1").arg(list->count()));

    // 点第 3 行 → currentStep==3 且 materialize 与参照一致
    list->setCurrentRow(3);
    r.check(doc->history().currentStep() == 3,
            QStringLiteral("点第 3 行后 currentStep=%1").arg(doc->history().currentStep()));
    r.check(PsTests::diffCount(layer->materialize(), refs[3]) == 0,
            QStringLiteral("点第 3 行后 materialize == 参照"));

    // trim：把上限调到极小，触发丢弃后列表重建
    const int beforeTrim = doc->history().stepCount();
    doc->history().setByteLimit(1);
    const int afterTrim = doc->history().stepCount();
    r.check(afterTrim < beforeTrim && afterTrim >= 1,
            QStringLiteral("trim 后 stepCount %1 → %2").arg(beforeTrim).arg(afterTrim));
    r.check(list->count() == afterTrim + 1,
            QStringLiteral("trim 后列表重建 count=%1 == stepCount+1=%2")
                .arg(list->count())
                .arg(afterTrim + 1));

    r.info(QStringLiteral("undopanel：行数/跳转/trim 重建均通过（trim 后 %1 行）")
               .arg(list->count()));
    return r.fails;
}
