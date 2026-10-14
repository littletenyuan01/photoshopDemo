/**
 * t_filterpanel.cpp — 图层滤镜栈面板（可见 / 可管）。
 *
 * 守的契约：面板不只是「画出来」，而是**真的驱动 domain**：
 *  ① 列出活动像素层的滤镜（含关键参数，否则一行「锐化」看不出区别）；
 *  ② 勾选框 ↔ 启用状态（同值不提交，避免空操作 undo）；
 *  ③ 上移/下移真的改顺序；④ 删除真的删；⑤ 每个动作各留一条可撤销的 undo。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "testutil.h"
#include "ui/filterdialog.h"
#include "ui/filterstackwidget.h"

#include <QDialog>
#include <QListWidget>
#include <QPushButton>

using namespace Ps;
using PsTests::Reporter;

int tFilterPanel()
{
    Reporter r;

    const int W = 64, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 3, 255), QStringLiteral("L"));
    Layer *layer = doc->layers().layerAt(1);
    if (!layer) {
        r.check(false, QStringLiteral("有活动像素层"));
        return r.fails;
    }

    FilterNode blur(OpName::BoxBlur);
    blur.setBlurRadius(3);
    FilterNode sharp(OpName::Sharpen);
    sharp.setSharpenStrength(2);
    doc->addLayerFilter(blur, /*pushUndo=*/false);
    doc->addLayerFilter(sharp, /*pushUndo=*/false);

    FilterStackWidget panel;
    panel.setContext(doc.get(), 1);

    auto *list = panel.findChild<QListWidget *>(QStringLiteral("filterList"));
    r.check(list != nullptr, QStringLiteral("面板里有滤镜列表"));
    if (!list)
        return r.fails;

    r.check(!panel.isHidden(), QStringLiteral("有滤镜时面板显示出来"));
    r.check(list->count() == 2, QStringLiteral("列出 2 个滤镜（实际 %1）").arg(list->count()));
    if (list->count() == 2) {
        r.check(list->item(0)->text().contains(QStringLiteral("半径 3")),
                QStringLiteral("第一行带参数：%1").arg(list->item(0)->text()));
        r.check(list->item(1)->text().contains(QStringLiteral("强度 2")),
                QStringLiteral("第二行带参数：%1").arg(list->item(1)->text()));
    }

    // —— 勾选框 → 文档里的启用状态 ——
    if (list->count() == 2) {
        list->item(0)->setCheckState(Qt::Unchecked);
        r.check(!layer->filters().at(0).isEnabled(),
                QStringLiteral("取消勾选 → 文档里该滤镜已禁用"));
        list->item(0)->setCheckState(Qt::Checked);
        r.check(layer->filters().at(0).isEnabled(), QStringLiteral("重新勾选 → 又启用"));
    }

    // —— 上移 → 顺序真的变了 ——
    if (list->count() == 2) {
        list->setCurrentRow(1);
        auto *up = panel.findChild<QPushButton *>(QStringLiteral("btnFilterUp"));
        if (up)
            up->click();
        r.check(layer->filters().count() == 2 && layer->filters().at(0).op() == OpName::Sharpen,
                QStringLiteral("上移 → 锐化排到最前（实际 %1）")
                    .arg(opNameTitle(layer->filters().at(0).op())));
    }

    // —— 删除 → 文档与列表同时少一个，且可撤销 ——
    if (list->count() == 2) {
        list->setCurrentRow(0);
        auto *del = panel.findChild<QPushButton *>(QStringLiteral("btnFilterDelete"));
        if (del)
            del->click();
        r.check(layer->filters().count() == 1 && list->count() == 1,
                QStringLiteral("删除 → 文档 %1 个 / 列表 %2 行")
                    .arg(layer->filters().count())
                    .arg(list->count()));
        doc->undo();
        r.check(layer->filters().count() == 2, QStringLiteral("撤销 → 被删的滤镜回来了"));
    }

    // —— 区域化预览的回落：面板不持有画布，靠文档里的「可见区提示」 ——
    {
        auto doc3 = ImageDocument::createBlank(256, 256, Qt::white);
        doc3->placeImageAsLayer(PsTests::makeLayerImage(256, 256, 7, 255), QStringLiteral("L3"));
        Layer *l3 = doc3->layers().layerAt(1);
        FilterNode noop(OpName::BoxBlur);
        noop.setBlurRadius(0);
        l3->filters().append(noop);

        Projection p;
        p.bind(doc3.get());
        p.setPriorityRect(QRect(0, 0, 256, 256));
        doc3->markDirty();
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage before = p.image();

        doc3->setPreviewRegionHint(QRect(0, 0, 64, 64)); // 假装画布报告「只看得到左上角」
        FilterNode blur(OpName::BoxBlur);
        blur.setBlurRadius(4);
        doc3->setLayerFilterNodePreview(1, 0, blur, QRect()); // 空 region → 回落到提示
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage after = p.image();

        const QRect in(0, 0, 64, 64);
        const QRect out(64, 0, 192, 256);
        const long long inD = PsTests::diffCount(before.copy(in), after.copy(in));
        const long long outD = PsTests::diffCount(before.copy(out), after.copy(out));
        r.check(inD > 0 && outD == 0,
                QStringLiteral("空 region → 用可见区提示（区内 %1 / 区外 %2）").arg(inD).arg(outD));
    }

    // —— 可发现性：没有滤镜时**不隐藏**，给一行占位提示（隐藏等于用户不知道有这功能）——
    {
        auto doc2 = ImageDocument::createBlank(W, H, Qt::white);
        doc2->placeImageAsLayer(PsTests::makeLayerImage(W, H, 5, 255), QStringLiteral("L2"));
        FilterStackWidget p2;
        p2.setContext(doc2.get(), 1);
        auto *list2 = p2.findChild<QListWidget *>(QStringLiteral("filterList"));
        r.check(!p2.isHidden(), QStringLiteral("无滤镜时面板仍显示（不隐藏）"));
        r.check(list2 && list2->count() == 1
                    && list2->item(0)->text().contains(QStringLiteral("暂无滤镜")),
                QStringLiteral("无滤镜时给出占位提示（实际 %1 行）")
                    .arg(list2 ? list2->count() : -1));
        auto *edit2 = p2.findChild<QPushButton *>(QStringLiteral("btnFilterEdit"));
        r.check(edit2 && !edit2->isEnabled(), QStringLiteral("无滤镜时编辑按钮禁用"));
    }

    // —— A10：编辑 A 层滤镜时切到 B 层再取消，不得把 A 的原参数写进 B ——
    {
        auto docA = ImageDocument::createBlank(W, H, Qt::white);
        docA->placeImageAsLayer(PsTests::makeLayerImage(W, H, 3, 255), QStringLiteral("A"));
        docA->placeImageAsLayer(PsTests::makeLayerImage(W, H, 5, 255), QStringLiteral("B"));
        Layer *layerA = docA->layers().layerAt(1);
        Layer *layerB = docA->layers().layerAt(2);
        r.check(layerA && layerB, QStringLiteral("A10：两像素层就绪"));
        if (layerA && layerB) {
            FilterNode fa(OpName::BoxBlur);
            fa.setBlurRadius(3);
            FilterNode fb(OpName::BoxBlur);
            fb.setBlurRadius(9);
            docA->setActiveLayerIndex(1);
            docA->addLayerFilter(fa, /*pushUndo=*/false);
            docA->setActiveLayerIndex(2);
            docA->addLayerFilter(fb, /*pushUndo=*/false);

            FilterStackWidget pA;
            pA.setContext(docA.get(), 1);
            auto *listA = pA.findChild<QListWidget *>(QStringLiteral("filterList"));
            auto *editA = pA.findChild<QPushButton *>(QStringLiteral("btnFilterEdit"));
            r.check(listA && editA && listA->count() == 1,
                    QStringLiteral("A10：A 层面板有 1 个滤镜"));
            if (listA && editA) {
                listA->setCurrentRow(0);
                editA->click();
                auto *dlg = pA.findChild<FilterDialog *>();
                r.check(dlg != nullptr, QStringLiteral("A10：编辑对话框已打开"));
                if (dlg) {
                    // 模拟切层：PropertiesPanel 会 setContext(B)
                    pA.setContext(docA.get(), 2);
                    dlg->reject();
                    r.check(layerB->filters().count() == 1
                                && layerB->filters().at(0).blurRadius() == 9,
                            QStringLiteral("A10：取消后 B 层半径仍为 9（实际 %1）")
                                .arg(layerB->filters().count() > 0
                                         ? layerB->filters().at(0).blurRadius()
                                         : -1));
                    r.check(layerA->filters().count() == 1
                                && layerA->filters().at(0).blurRadius() == 3,
                            QStringLiteral("A10：A 层半径仍为 3"));
                }
            }
        }
    }

    return r.fails;
}
