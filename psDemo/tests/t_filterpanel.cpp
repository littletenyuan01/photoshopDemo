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
#include "testutil.h"
#include "ui/filterstackwidget.h"

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

    // —— 没有滤镜时面板隐藏（不占地方）——
    {
        auto doc2 = ImageDocument::createBlank(W, H, Qt::white);
        doc2->placeImageAsLayer(PsTests::makeLayerImage(W, H, 5, 255), QStringLiteral("L2"));
        FilterStackWidget p2;
        p2.setContext(doc2.get(), 1);
        r.check(p2.isHidden(), QStringLiteral("无滤镜时面板隐藏"));
    }

    return r.fails;
}
