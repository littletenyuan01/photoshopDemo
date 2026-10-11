/**
 * t_filterundo.cpp — 滤镜对话框的**撤销原子性**（离线复现「撤不回来」的反馈）。
 *
 * 契约：
 *  · 点确定 → **一条**撤销即回到「加滤镜之前」（不是要按两次，也不是只回退半径）；
 *  · 点取消 → 文档回到原样，且流程本身**不往撤销栈塞条目**。
 *
 * 背景：早先的流程在「打开对话框」时就 push 了一条 undo，取消时又 push 一条删除，
 * 于是取消后按 Ctrl+Z 会把刚取消掉的滤镜**撤回来**（用户感受就是「撤不回来/反了」）。
 */
#include "app/historystack.h"
#include "domain/filternode.h"
#include "engine/projection.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/op/opname.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

int tFilterUndo()
{
    Reporter r;

    const int W = 64, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("L"));

    const QImage before = Compositor::composite(*doc);
    const int baseCount = doc->layers().layerAt(1)->filters().count();

    // 复刻 MainWindow::runFilterDialog 的流程（临时节点 → 预览 → 确定/取消）
    auto runDialog = [&](bool accept) -> int {
        FilterNode node(OpName::BoxBlur);
        node.setBlurRadius(5);
        const int fi = doc->addLayerFilter(node, /*pushUndo=*/false);
        if (fi < 0)
            return -1;
        node.setBlurRadius(9);
        doc->setLayerFilterNode(1, fi, node, /*pushUndo=*/false); // 拖动预览
        if (accept) {
            doc->removeLayerFilter(1, fi, /*pushUndo=*/false);
            return doc->addLayerFilter(node, /*pushUndo=*/true); // 正式加入：唯一一条 undo
        }
        doc->removeLayerFilter(1, fi, /*pushUndo=*/false); // 取消：不留痕
        return -1;
    };

    // —— 场景 A：点确定 → 一条撤销回到原样 ——
    {
        const int fi = runDialog(/*accept=*/true);
        r.check(fi >= 0, QStringLiteral("确定：滤镜已正式加入"));
        const long long dOk = PsTests::diffCount(before, Compositor::composite(*doc));
        r.check(dOk > 0, QStringLiteral("确定后画面已变化（diffs=%1）").arg(dOk));

        doc->undo();

        const int cnt = doc->layers().layerAt(1)->filters().count();
        const long long dUndo = PsTests::diffCount(before, Compositor::composite(*doc));
        r.check(cnt == baseCount && dUndo == 0,
                QStringLiteral("一次撤销即回到原样（filters=%1 diffs=%2）").arg(cnt).arg(dUndo));
    }

    // —— 场景 B：点取消 → 状态原样，且不往撤销栈塞条目 ——
    {
        const bool canUndoBefore = doc->history().canUndo();
        runDialog(/*accept=*/false);
        const int cnt = doc->layers().layerAt(1)->filters().count();
        const long long dCancel = PsTests::diffCount(before, Compositor::composite(*doc));
        r.check(cnt == baseCount && dCancel == 0,
                QStringLiteral("取消后回到原样（filters=%1 diffs=%2）").arg(cnt).arg(dCancel));
        r.check(doc->history().canUndo() == canUndoBefore,
                QStringLiteral("取消流程不改变撤销栈可用性"));
    }

    // —— 场景 C：滤镜预览**只刷新给定区域**（拖动时不重算整层，大文档才不卡）——
    {
        const int W = 256, H = 256;
        auto d3 = ImageDocument::createBlank(W, H, Qt::white);
        d3->placeImageAsLayer(PsTests::makeLayerImage(W, H, 31, 255), QStringLiteral("L"));
        Layer *l3 = d3->layers().layerAt(1);

        // 先挂一个「恒等」滤镜（半径 0），让投影整体填满一次
        FilterNode noop(OpName::BoxBlur);
        noop.setBlurRadius(0);
        l3->filters().append(noop);

        Projection p;
        p.bind(d3.get());
        p.setPriorityRect(QRect(0, 0, W, H));
        d3->markDirty();
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage beforeRegion = p.image();

        // 只在左上 64×64（正好一个 64×64 块）上预览一个有效果的半径
        const QRect region(0, 0, 64, 64);
        FilterNode blur(OpName::BoxBlur);
        blur.setBlurRadius(4);
        d3->setLayerFilterNodePreview(1, 0, blur, region);
        p.sync(); // 投影在 sync 里取脏区（不调它就拿不到这次 markDirty）
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
        const QImage afterRegion = p.image();

        const QRect outside(64, 0, W - 64, H);
        const long long inDiff =
            PsTests::diffCount(beforeRegion.copy(region), afterRegion.copy(region));
        const long long outDiff =
            PsTests::diffCount(beforeRegion.copy(outside), afterRegion.copy(outside));
        r.check(inDiff > 0, QStringLiteral("区域预览：区内已刷新（diffs=%1）").arg(inDiff));
        r.check(outDiff == 0,
                QStringLiteral("区域预览：区外保持旧像素、未重算（diffs=%1）").arg(outDiff));
    }

    return r.fails;
}
