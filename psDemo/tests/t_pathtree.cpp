/**
 * t_pathtree.cpp — 路径树 domain API：新建/删除/选区互转/撤销（pen-tool-plan P3）。
 * 面板本身是 Qt 控件，此处直调 ImageDocument 语义接口（与 t_filterpanel 同口径）。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "domain/path.h"
#include "engine/op/pathfillop.h"
#include "engine/selectiontopath.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

namespace {

Path makeRectPath(const QRectF &r)
{
    Path path;
    path.name = QStringLiteral("rect");
    SubPath sp;
    sp.closed = true;
    const QPointF pts[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    for (const QPointF &p : pts) {
        PathAnchor a;
        a.point = a.inHandle = a.outHandle = p;
        sp.anchors.append(a);
    }
    path.subPaths.append(sp);
    return path;
}

} // namespace

int tPathTree()
{
    Reporter r;
    const int W = 120, H = 100;

    // 新建 / 删除各留一条撤销
    {
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        const int d0 = doc->history().undoDepth();
        doc->pushPathUndo(QStringLiteral("新建路径"));
        doc->addPath(Path{});
        r.check(doc->paths().count() == 1, QStringLiteral("新建后路径数=1"));
        r.check(doc->history().undoDepth() == d0 + 1, QStringLiteral("新建压一条撤销"));
        doc->pushPathUndo(QStringLiteral("删除路径"));
        doc->removePath(0);
        r.check(doc->paths().count() == 0, QStringLiteral("删除后路径数=0"));
        r.check(doc->history().undoDepth() == d0 + 2, QStringLiteral("删除再压一条"));
        doc->undo();
        r.check(doc->paths().count() == 1, QStringLiteral("撤销删除 → 恢复"));
        doc->undo();
        r.check(doc->paths().count() == 0, QStringLiteral("撤销新建 → 空"));
    }

    // 名称 / 眼睛驱动 domain
    {
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->pushPathUndo(QStringLiteral("新建"));
        doc->addPath(makeRectPath(QRectF(10, 10, 40, 30)));
        doc->pushPathUndo(QStringLiteral("改名"));
        doc->setPathName(0, QStringLiteral("Alpha"));
        doc->pushPathUndo(QStringLiteral("隐藏"));
        doc->setPathVisible(0, false);
        r.check(doc->paths().pathAt(0)->name == QStringLiteral("Alpha"),
                QStringLiteral("名称写入 domain"));
        r.check(doc->paths().pathAt(0)->visible == false, QStringLiteral("眼睛写入 domain"));
    }

    // 路径 → 选区
    {
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->pushPathUndo(QStringLiteral("新建"));
        doc->addPath(makeRectPath(QRectF(20, 20, 50, 40)));
        const int d0 = doc->history().undoDepth();
        r.check(doc->pathToSelection(ChannelOp::Replace), QStringLiteral("路径→选区成功"));
        r.check(!doc->selection().isEmpty(), QStringLiteral("选区非空"));
        r.check(doc->selection().value(45, 40) > 200, QStringLiteral("路径内部被选中"));
        r.check(doc->history().undoDepth() == d0 + 1, QStringLiteral("路径→选区一条撤销"));
    }

    // 选区 → 路径
    {
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->selectRectangle(QRect(15, 15, 60, 50), ChannelOp::Replace);
        const int d0 = doc->history().undoDepth();
        const int idx = doc->selectionToPath();
        r.check(idx >= 0, QStringLiteral("选区→路径成功"));
        r.check(doc->paths().count() == 1, QStringLiteral("产出 1 条路径"));
        r.check(!doc->paths().pathAt(0)->isEmpty(), QStringLiteral("路径非空"));
        r.check(doc->history().undoDepth() == d0 + 1, QStringLiteral("选区→路径一条撤销"));

        // 轮廓算法冒烟：矩形 mask 至少得到一个闭合子路径
        const Path direct = SelectionToPath::fromMask(doc->selection().mask());
        r.check(direct.subPaths.size() >= 1 && direct.subPaths[0].closed,
                QStringLiteral("Moore 轮廓得闭合子路径"));
    }

    // 路径→选区→再路径：可重入
    {
        auto doc = ImageDocument::createBlank(W, H, Qt::white);
        doc->addPath(makeRectPath(QRectF(25, 25, 40, 40)));
        doc->pathToSelection(ChannelOp::Replace);
        doc->selectionToPath();
        r.check(doc->paths().count() == 2, QStringLiteral("互转后路径数=2"));
        r.check(!doc->selection().isEmpty(), QStringLiteral("选区仍在"));
    }

    return r.fails;
}
