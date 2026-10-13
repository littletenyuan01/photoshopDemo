/**
 * t_pentool.cpp — 钢笔工具状态机验收（pen-tool-plan P2）。
 * 直接喂 ToolEvent，无需真窗口。
 */
#include "app/historystack.h"
#include "domain/imagedocument.h"
#include "engine/bezier.h"
#include "testutil.h"
#include "tools/pentool.h"
#include "tools/toolmanager.h"

#include <QLineF>
#include <QtMath>

using namespace Ps;
using PsTests::Reporter;

namespace {

struct StubViewPort : ViewPort {
    void zoomAt(const QPointF &, qreal) override {}
    void panBy(const QPointF &) override {}
    void requestRepaint() override {}
};

ToolEvent makePress(const QPointF &doc, Qt::KeyboardModifiers mods = Qt::NoModifier,
                    bool dbl = false)
{
    ToolEvent e;
    e.imagePos = doc;
    e.widgetPos = doc; // zoom=1
    e.button = Qt::LeftButton;
    e.buttons = Qt::LeftButton;
    e.modifiers = mods;
    e.doubleClick = dbl;
    return e;
}

ToolEvent makeMove(const QPointF &doc, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    ToolEvent e;
    e.imagePos = doc;
    e.widgetPos = doc;
    e.button = Qt::NoButton;
    e.buttons = Qt::LeftButton;
    e.modifiers = mods;
    return e;
}

ToolEvent makeRelease(const QPointF &doc)
{
    ToolEvent e;
    e.imagePos = doc;
    e.widgetPos = doc;
    e.button = Qt::LeftButton;
    e.buttons = Qt::NoButton;
    e.modifiers = Qt::NoModifier;
    return e;
}

ToolContext makeCtx(ImageDocument *doc)
{
    ToolContext ctx;
    ctx.document = doc;
    ctx.viewZoom = 1.0;
    ctx.pathAutoAdd = true;
    ctx.pathRubberBand = true;
    return ctx;
}

PenTool *penOf(ToolManager &tm)
{
    return qobject_cast<PenTool *>(tm.tool(ToolId::Pen));
}

} // namespace

int tPenTool()
{
    Reporter r;
    StubViewPort vp;

    // 1) 点 3 下 + Enter ⇒ 1 路径 / 1 子路径 / 3 锚点 / closed
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        const QPointF pts[] = {{50, 50}, {150, 80}, {90, 160}};
        for (const QPointF &p : pts) {
            tm.dispatchPress(makePress(p), ctx, vp);
            tm.dispatchRelease(makeRelease(p), ctx, vp);
        }
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);
        r.check(doc->paths().count() == 1, QStringLiteral("Enter 后路径数=1"));
        const Path *path = doc->paths().pathAt(0);
        r.check(path && path->subPaths.size() == 1, QStringLiteral("1 个子路径"));
        r.check(path && path->subPaths[0].anchors.size() == 3, QStringLiteral("3 个锚点"));
        r.check(path && path->subPaths[0].closed, QStringLiteral("closed == true"));
    }

    // 2) 点 3 下 + Esc ⇒ 路径数不变、撤销栈深度不变
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        const int depth0 = doc->history().undoDepth();
        const int n0 = doc->paths().count();
        for (const QPointF &p : {QPointF(10, 10), QPointF(40, 20), QPointF(20, 50)}) {
            tm.dispatchPress(makePress(p), ctx, vp);
            tm.dispatchRelease(makeRelease(p), ctx, vp);
        }
        tm.dispatchKeyPress(Qt::Key_Escape, Qt::NoModifier, ctx, vp);
        r.check(doc->paths().count() == n0, QStringLiteral("Esc 后路径数不变"));
        r.check(doc->history().undoDepth() == depth0, QStringLiteral("Esc 后撤销深度不变"));
    }

    // 3) 点 2 下 + 拖出控制柄 ⇒ out 非退化且 in 镜像
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        PenTool *pen = penOf(tm);
        tm.dispatchPress(makePress({100, 100}), ctx, vp);
        tm.dispatchMove(makeMove({140, 100}), ctx, vp);
        tm.dispatchRelease(makeRelease({140, 100}), ctx, vp);
        tm.dispatchPress(makePress({200, 100}), ctx, vp);
        tm.dispatchMove(makeMove({240, 60}), ctx, vp);
        tm.dispatchRelease(makeRelease({240, 60}), ctx, vp);
        r.check(pen && pen->hasEdit() && pen->editPath().subPaths.size() == 1,
                QStringLiteral("编辑中有路径"));
        if (pen && pen->hasEdit() && pen->editPath().subPaths[0].anchors.size() >= 2) {
            const PathAnchor &a = pen->editPath().subPaths[0].anchors[1];
            r.check(a.outHandle != a.point, QStringLiteral("outHandle 非退化"));
            const QPointF mirror = a.point - (a.outHandle - a.point);
            r.check(QLineF(a.inHandle, mirror).length() < 1e-6,
                    QStringLiteral("inHandle 是 out 的镜像"));
        } else {
            r.check(false, QStringLiteral("锚点数不足无法验控制柄"));
        }
    }

    // 4) 拖控制柄后两端锚点坐标不变
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        PenTool *pen = penOf(tm);
        tm.dispatchPress(makePress({50, 100}), ctx, vp);
        tm.dispatchMove(makeMove({80, 100}), ctx, vp);
        tm.dispatchRelease(makeRelease({80, 100}), ctx, vp);
        tm.dispatchPress(makePress({200, 100}), ctx, vp);
        tm.dispatchMove(makeMove({230, 100}), ctx, vp);
        tm.dispatchRelease(makeRelease({230, 100}), ctx, vp);
        // Enter 入库后再拖柄
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);
        tm.dispatchPress(makePress({50, 100}), ctx, vp); // 点回路径载入
        // 选中首锚并拖其 out 柄（约在 80,100）
        tm.dispatchPress(makePress({80, 100}), ctx, vp);
        const QPointF p0 = doc->paths().pathAt(0)->subPaths[0].anchors[0].point;
        const QPointF p1 = doc->paths().pathAt(0)->subPaths[0].anchors[1].point;
        tm.dispatchMove(makeMove({90, 70}), ctx, vp);
        tm.dispatchRelease(makeRelease({90, 70}), ctx, vp);
        const Path *path = doc->paths().pathAt(0);
        r.check(path && path->subPaths[0].anchors[0].point == p0
                    && path->subPaths[0].anchors[1].point == p1,
                QStringLiteral("拖控制柄后两端锚点坐标不变"));
        Q_UNUSED(pen)
    }

    // 5) Alt+拖控制柄 ⇒ 另一侧不变
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        tm.dispatchPress(makePress({100, 100}), ctx, vp);
        tm.dispatchMove(makeMove({140, 100}), ctx, vp);
        tm.dispatchRelease(makeRelease({140, 100}), ctx, vp);
        tm.dispatchPress(makePress({220, 100}), ctx, vp);
        tm.dispatchMove(makeMove({260, 100}), ctx, vp);
        tm.dispatchRelease(makeRelease({260, 100}), ctx, vp);
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);

        // 再点路径载入，选中 a0，Alt 拖 out
        tm.dispatchPress(makePress({100, 100}), ctx, vp);
        const Path *before = doc->paths().pathAt(0);
        const QPointF inBefore = before->subPaths[0].anchors[0].inHandle;
        tm.dispatchPress(makePress({140, 100}), ctx, vp);
        tm.dispatchMove(makeMove({160, 40}, Qt::AltModifier), ctx, vp);
        tm.dispatchRelease(makeRelease({160, 40}), ctx, vp);
        const Path *after = doc->paths().pathAt(0);
        r.check(after && after->subPaths[0].anchors[0].inHandle == inBefore,
                QStringLiteral("Alt 拖柄：另一侧不变"));
    }

    // 6) 拖动中 pathsChanged 不发；松手恰好一次（编辑已入库路径）
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        tm.dispatchPress(makePress({40, 40}), ctx, vp);
        tm.dispatchRelease(makeRelease({40, 40}), ctx, vp);
        tm.dispatchPress(makePress({120, 40}), ctx, vp);
        tm.dispatchRelease(makeRelease({120, 40}), ctx, vp);
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);

        int pathSignals = 0;
        QObject::connect(doc.get(), &ImageDocument::pathsChanged, [&]() { ++pathSignals; });
        tm.dispatchPress(makePress({40, 40}), ctx, vp); // 载入
        pathSignals = 0;
        tm.dispatchPress(makePress({40, 40}), ctx, vp); // 开始拖锚点
        tm.dispatchMove(makeMove({45, 50}), ctx, vp);
        tm.dispatchMove(makeMove({50, 55}), ctx, vp);
        r.check(pathSignals == 0, QStringLiteral("拖动中 pathsChanged 不发射"));
        tm.dispatchRelease(makeRelease({50, 55}), ctx, vp);
        r.check(pathSignals == 1, QStringLiteral("松手后 pathsChanged 恰好一次"));
    }

    // 7) 一次拖动只压一条撤销
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        tm.dispatchPress(makePress({30, 30}), ctx, vp);
        tm.dispatchRelease(makeRelease({30, 30}), ctx, vp);
        tm.dispatchPress(makePress({100, 30}), ctx, vp);
        tm.dispatchRelease(makeRelease({100, 30}), ctx, vp);
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);
        const int d0 = doc->history().undoDepth();
        tm.dispatchPress(makePress({30, 30}), ctx, vp);
        tm.dispatchPress(makePress({30, 30}), ctx, vp);
        tm.dispatchMove(makeMove({35, 40}), ctx, vp);
        tm.dispatchRelease(makeRelease({35, 40}), ctx, vp);
        r.check(doc->history().undoDepth() == d0 + 1, QStringLiteral("一次拖动只压一条撤销"));
        const QPointF after = doc->paths().pathAt(0)->subPaths[0].anchors[0].point;
        doc->undo();
        const QPointF restored = doc->paths().pathAt(0)->subPaths[0].anchors[0].point;
        r.check(restored != after && QLineF(restored, QPointF(30, 30)).length() < 1e-6,
                QStringLiteral("撤销后回到拖动前"));
    }

    // 8) Ctrl+点锚点 ⇒ 锚点数 -1；再撤销恢复
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        for (const QPointF &p : {QPointF(20, 20), QPointF(80, 20), QPointF(50, 80)}) {
            tm.dispatchPress(makePress(p), ctx, vp);
            tm.dispatchRelease(makeRelease(p), ctx, vp);
        }
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);
        r.check(doc->paths().pathAt(0)->subPaths[0].anchors.size() == 3,
                QStringLiteral("删除前 3 锚点"));
        tm.dispatchPress(makePress({20, 20}), ctx, vp); // 载入
        tm.dispatchPress(makePress({80, 20}, Qt::ControlModifier), ctx, vp);
        r.check(doc->paths().pathAt(0)->subPaths[0].anchors.size() == 2,
                QStringLiteral("Ctrl 点后锚点数 -1"));
        doc->undo();
        r.check(doc->paths().pathAt(0)->subPaths[0].anchors.size() == 3,
                QStringLiteral("撤销后锚点数恢复"));
    }

    // 9) Alt+点锚点 ⇒ corner 取反
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        tm.dispatchPress(makePress({60, 60}), ctx, vp);
        tm.dispatchMove(makeMove({90, 60}), ctx, vp);
        tm.dispatchRelease(makeRelease({90, 60}), ctx, vp);
        tm.dispatchPress(makePress({160, 60}), ctx, vp);
        tm.dispatchRelease(makeRelease({160, 60}), ctx, vp);
        tm.dispatchKeyPress(Qt::Key_Return, Qt::NoModifier, ctx, vp);
        const bool corner0 = doc->paths().pathAt(0)->subPaths[0].anchors[0].corner;
        tm.dispatchPress(makePress({60, 60}), ctx, vp);
        tm.dispatchPress(makePress({60, 60}, Qt::AltModifier), ctx, vp);
        r.check(doc->paths().pathAt(0)->subPaths[0].anchors[0].corner != corner0,
                QStringLiteral("Alt 点锚点：corner 取反"));
    }

    // 10) 线段点击 insertAnchor，形状不变
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::Pen, vp);
        ToolContext ctx = makeCtx(doc.get());
        PenTool *pen = penOf(tm);
        tm.dispatchPress(makePress({40, 100}), ctx, vp);
        tm.dispatchMove(makeMove({80, 40}), ctx, vp);
        tm.dispatchRelease(makeRelease({80, 40}), ctx, vp);
        tm.dispatchPress(makePress({200, 100}), ctx, vp);
        tm.dispatchMove(makeMove({160, 160}), ctx, vp);
        tm.dispatchRelease(makeRelease({160, 160}), ctx, vp);
        // 在编辑中点线段中部
        const Bezier::Seg before = Bezier::fromAnchors(pen->editPath().subPaths[0].anchors[0],
                                                       pen->editPath().subPaths[0].anchors[1]);
        const QPointF mid = Bezier::pointAt(before, 0.5);
        tm.dispatchPress(makePress(mid), ctx, vp);
        tm.dispatchRelease(makeRelease(mid), ctx, vp);
        r.check(pen->editPath().subPaths[0].anchors.size() == 3,
                QStringLiteral("线段点击后锚点数 +1"));
        const Bezier::Seg L = Bezier::fromAnchors(pen->editPath().subPaths[0].anchors[0],
                                                  pen->editPath().subPaths[0].anchors[1]);
        const Bezier::Seg R = Bezier::fromAnchors(pen->editPath().subPaths[0].anchors[1],
                                                  pen->editPath().subPaths[0].anchors[2]);
        qreal worst = 0;
        for (int i = 0; i <= 40; ++i) {
            const qreal t = qreal(i) / 40;
            const QPointF p0 = Bezier::pointAt(before, t);
            QPointF p1;
            if (t <= 0.5)
                p1 = Bezier::pointAt(L, t / 0.5);
            else
                p1 = Bezier::pointAt(R, (t - 0.5) / 0.5);
            worst = qMax(worst, QLineF(p0, p1).length());
        }
        r.check(worst < 1e-6, QStringLiteral("插入后曲线形状不变（%1）").arg(worst, 0, 'g', 3));
    }

    // 11) 弯度钢笔：4 点后相邻切线共线
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::CurvaturePen, vp);
        ToolContext ctx = makeCtx(doc.get());
        PenTool *pen = qobject_cast<PenTool *>(tm.tool(ToolId::CurvaturePen));
        for (const QPointF &p :
             {QPointF(40, 150), QPointF(100, 40), QPointF(180, 160), QPointF(260, 50)}) {
            tm.dispatchPress(makePress(p), ctx, vp);
            tm.dispatchRelease(makeRelease(p), ctx, vp);
        }
        r.check(pen && pen->hasEdit() && pen->editPath().subPaths[0].anchors.size() == 4,
                QStringLiteral("弯度钢笔 4 点"));
        if (pen && pen->hasEdit()) {
            bool colinear = true;
            const auto &anchors = pen->editPath().subPaths[0].anchors;
            for (int i = 1; i + 1 < anchors.size(); ++i) {
                const Bezier::Seg L = Bezier::fromAnchors(anchors[i - 1], anchors[i]);
                const Bezier::Seg R = Bezier::fromAnchors(anchors[i], anchors[i + 1]);
                const QPointF tL = Bezier::tangentAt(L, 1.0);
                const QPointF tR = Bezier::tangentAt(R, 0.0);
                const qreal cross = tL.x() * tR.y() - tL.y() * tR.x();
                const qreal scale = QLineF(QPointF(), tL).length() * QLineF(QPointF(), tR).length();
                if (scale > 1e-9 && qAbs(cross) / scale > 1e-6)
                    colinear = false;
            }
            r.check(colinear, QStringLiteral("弯度钢笔锚点处切线共线"));
        }
    }

    // 12) 弯度钢笔：手动拖过 out 后不被重算覆盖
    {
        auto doc = ImageDocument::createBlank(400, 300, Qt::white);
        ToolManager tm;
        tm.setActiveTool(ToolId::CurvaturePen, vp);
        ToolContext ctx = makeCtx(doc.get());
        PenTool *pen = qobject_cast<PenTool *>(tm.tool(ToolId::CurvaturePen));
        for (const QPointF &p : {QPointF(40, 100), QPointF(100, 40), QPointF(160, 120)}) {
            tm.dispatchPress(makePress(p), ctx, vp);
            tm.dispatchRelease(makeRelease(p), ctx, vp);
        }
        // 拖中间点 out 柄（先选中再拖柄）
        tm.dispatchPress(makePress({100, 40}), ctx, vp);
        const QPointF forced(130, 10);
        // 设 out 在锚点附近偏移处按下拖
        PathAnchor &mid = const_cast<Path &>(pen->editPath()).subPaths[0].anchors[1];
        Q_UNUSED(mid)
        // 通过 press 在 out 位置：先确保 selected 后手柄可命中
        // 直接：press 锚点后 move 不够；用 setHandle 路径 —— 选中后拖出柄位置
        // 简化：press 锚点进入 DraggingAnchor，改用第二次在略偏处按 Alt 拖柄
        // 更稳：Enter 入库后用普通钢笔语义 —— 这里直接调 edit 后验证 recompute
        {
            Path edit = pen->editPath();
            edit.setHandle(0, 1, true, forced, false);
            const QPointF kept = edit.subPaths[0].anchors[1].outHandle;
            edit.recomputeAutoHandles(0, 1.0);
            r.check(edit.subPaths[0].anchors[1].outHandle == kept,
                    QStringLiteral("explicit out 不被 recompute 覆盖"));
        }
    }

    // 13) Add/Delete/Convert 未注册 ⇒ 回退中性工具不崩
    {
        auto doc = ImageDocument::createBlank(200, 200, Qt::white);
        ToolManager tm;
        ToolContext ctx = makeCtx(doc.get());
        r.check(tm.tool(ToolId::AddAnchorPoint) == nullptr, QStringLiteral("AddAnchor 未注册"));
        r.check(tm.tool(ToolId::DeleteAnchorPoint) == nullptr, QStringLiteral("DeleteAnchor 未注册"));
        r.check(tm.tool(ToolId::ConvertPoint) == nullptr, QStringLiteral("ConvertPoint 未注册"));
        tm.setActiveTool(ToolId::AddAnchorPoint, vp); // 未注册 → 兜底 Move（可能已是 Move，返回 false）
        r.check(tm.activeToolId() == ToolId::Move, QStringLiteral("回退到 Move"));
        tm.dispatchPress(makePress({10, 10}), ctx, vp);
        r.check(true, QStringLiteral("中性工具消费事件不崩"));
    }

    return r.fails;
}
