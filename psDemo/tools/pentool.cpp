/**
 * pentool.cpp — 钢笔 / 自由钢笔 / 弯度钢笔实现。
 *
 * 修饰键对照 GIMP path-enums FEATURE_NONE / ALIGNED（见 pen-tool-plan §5.4）。
 */
#include "pentool.h"

#include "domain/imagedocument.h"
#include "engine/bezier.h"
#include "patheditops.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace Ps {

namespace {

constexpr qreal kTolWidget = 8.0;
constexpr qreal kAnchorHalf = 3.5;   // 7×7 方块半边（控件像素）
constexpr qreal kHandleR = 2.5;      // 控制柄圆半径
constexpr qreal kFreeformStep = 3.0; // 自由钢笔距离阈值（文档像素）
constexpr qreal kCurvatureTension = 1.0;

PathAnchor makeCornerAnchor(const QPointF &p)
{
    PathAnchor a;
    a.point = p;
    a.inHandle = p;
    a.outHandle = p;
    return a;
}

} // namespace

PenTool::PenTool(Ps::ToolId id, QObject *parent)
    : Tool(id, parent)
    , m_isCurvature(id == Ps::ToolId::CurvaturePen)
    , m_isFreeform(id == Ps::ToolId::FreeformPen)
{
}

Qt::CursorShape PenTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool PenTool::hasOverlay() const
{
    return m_hasEdit || m_state != State::Idle;
}

qreal PenTool::tolDoc(const ToolContext &ctx) const
{
    return kTolWidget / qMax(ctx.viewZoom, 1e-6);
}

void PenTool::ensureEditSub()
{
    if (!m_hasEdit) {
        m_edit = Path{};
        m_edit.name = QStringLiteral("工作路径");
        m_hasEdit = true;
        m_docIndex = -1;
    }
    if (m_edit.subPaths.isEmpty()) {
        SubPath sp;
        sp.closed = false;
        m_edit.subPaths.append(sp);
    }
    if (m_activeSub < 0 || m_activeSub >= m_edit.subPaths.size())
        m_activeSub = m_edit.subPaths.size() - 1;
}

void PenTool::clearSelectionFlags()
{
    for (SubPath &sp : m_edit.subPaths) {
        for (PathAnchor &a : sp.anchors)
            a.selected = false;
    }
}

void PenTool::selectAnchor(int sub, int idx)
{
    clearSelectionFlags();
    if (sub < 0 || sub >= m_edit.subPaths.size())
        return;
    auto &anchors = m_edit.subPaths[sub].anchors;
    if (idx < 0 || idx >= anchors.size())
        return;
    anchors[idx].selected = true;
    m_activeSub = sub;
    m_activeIdx = idx;
}

void PenTool::appendAnchorAt(const QPointF &docPos)
{
    ensureEditSub();
    SubPath &sp = m_edit.subPaths[m_activeSub];
    PathAnchor a = makeCornerAnchor(docPos);
    a.selected = true;
    clearSelectionFlags();
    sp.anchors.append(a);
    m_activeIdx = sp.anchors.size() - 1;
    m_dirtySinceCommit = true;

    if (m_isCurvature)
        m_edit.recomputeAutoHandles(m_activeSub, kCurvatureTension);
}

QPointF PenTool::snap45(const QPointF &origin, const QPointF &pos)
{
    const QPointF d = pos - origin;
    const qreal len = QLineF(origin, pos).length();
    if (len < 1e-6)
        return pos;
    qreal ang = qAtan2(d.y(), d.x());
    const qreal step = M_PI / 4.0;
    ang = qRound(ang / step) * step;
    return origin + QPointF(qCos(ang) * len, qSin(ang) * len);
}

PenTool::Target PenTool::classify(const ToolEvent &e, const ToolContext &ctx) const
{
    if (!m_hasEdit)
        return Target::Empty;
    const qreal tol = tolDoc(ctx);
    const Path::HandleHit hh = m_edit.hitTestHandle(e.imagePos, tol, true);
    if (hh.sub >= 0)
        return hh.isOut ? Target::HandleOut : Target::HandleIn;
    const Path::Hit ah = m_edit.hitTestAnchor(e.imagePos, tol);
    if (ah.sub >= 0)
        return Target::Anchor;
    const Path::SegHit sh = m_edit.nearestOnPath(e.imagePos, tol);
    if (sh.sub >= 0)
        return Target::Segment;
    return Target::Empty;
}

void PenTool::loadActiveFromDocument(const ToolContext &ctx)
{
    if (!ctx.document)
        return;
    Path *p = ctx.document->activePath();
    if (!p)
        return;
    m_edit = *p;
    m_hasEdit = true;
    m_docIndex = ctx.document->activePathIndex();
    m_activeSub = m_edit.subPaths.isEmpty() ? -1 : 0;
    m_activeIdx = -1;
    m_dirtySinceCommit = false;
}

void PenTool::clampActiveAnchor()
{
    if (m_activeSub < 0 || m_activeSub >= m_edit.subPaths.size()) {
        m_activeSub = -1;
        m_activeIdx = -1;
        return;
    }
    const int n = m_edit.subPaths[m_activeSub].anchors.size();
    if (n <= 0) {
        m_activeIdx = -1;
        return;
    }
    if (m_activeIdx >= n)
        m_activeIdx = n - 1;
    // m_activeIdx < 0 保持 -1（无选中）合法
}

void PenTool::commitEditToDocument(const ToolContext &ctx, const QString &label)
{
    if (!ctx.document || !m_hasEdit)
        return;
    // 至少两点才入库；单点工作路径不写入
    if (m_edit.totalAnchors() < 2)
        return;

    ctx.document->pushPathUndo(label);
    if (m_docIndex < 0) {
        m_docIndex = ctx.document->addPath(m_edit);
    } else {
        ctx.document->setPathAt(m_docIndex, m_edit);
        ctx.document->setActivePathIndex(m_docIndex);
    }
    m_dirtySinceCommit = false;
}

void PenTool::finishPath(const ToolContext &ctx, bool close)
{
    if (!m_hasEdit)
        return;
    if (m_activeSub >= 0 && m_activeSub < m_edit.subPaths.size())
        m_edit.subPaths[m_activeSub].closed = close;
    m_dirtySinceCommit = true;
    commitEditToDocument(ctx, close ? QObject::tr("闭合路径") : QObject::tr("结束路径"));
    // 结束后保留为文档路径；清空会话，下次点空白开新路径
    m_hasEdit = false;
    m_edit = Path{};
    m_docIndex = -1;
    m_activeSub = -1;
    m_activeIdx = -1;
    m_state = State::Idle;
    emit repaintRequested();
}

void PenTool::discardEdit()
{
    m_hasEdit = false;
    m_edit = Path{};
    m_docIndex = -1;
    m_activeSub = -1;
    m_activeIdx = -1;
    m_state = State::Idle;
    m_dirtySinceCommit = false;
}

bool PenTool::mousePress(const ToolEvent &e, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!e.isLeft() || !ctx.document)
        return false;

    m_cursorDoc = e.imagePos;
    const bool alt = e.modifiers.testFlag(Qt::AltModifier);
    const bool ctrl = e.modifiers.testFlag(Qt::ControlModifier);
    const bool shift = e.modifiers.testFlag(Qt::ShiftModifier);
    Q_UNUSED(shift)

    if (e.doubleClick) {
        if (m_hasEdit)
            finishPath(ctx, false);
        return true;
    }

    // —— 自由钢笔：按下开始采样 ——
    if (m_isFreeform && m_state == State::Idle) {
        ensureEditSub();
        appendAnchorAt(e.imagePos);
        m_state = State::FreeformDrawing;
        m_dragStart = e.imagePos;
        emit repaintRequested();
        return true;
    }

    // 若无会话，尝试载入活动路径以便继续编辑
    if (!m_hasEdit)
        loadActiveFromDocument(ctx);

    const Target t = classify(e, ctx);

    // Ctrl+点锚点/线段 → 删除（pathAutoAdd 开时）；几何走 PathEditOps
    if (ctrl && ctx.pathAutoAdd && m_hasEdit) {
        if (t == Target::Anchor) {
            const Path::Hit ah = m_edit.hitTestAnchor(e.imagePos, tolDoc(ctx));
            if (ah.sub >= 0) {
                PathEditOps::deleteAnchorAt(m_edit, ah.sub, ah.index);
                // A18：删除后钳制活动下标，避免越界读
                if (m_activeSub == ah.sub && m_activeIdx > ah.index)
                    --m_activeIdx;
                else if (m_activeSub == ah.sub && m_activeIdx == ah.index)
                    m_activeIdx = ah.index; // 将由 clamp 收到 n-1 或 -1
                clampActiveAnchor();
                m_dirtySinceCommit = true;
                if (m_docIndex >= 0)
                    commitEditToDocument(ctx, QObject::tr("删除锚点"));
                emit repaintRequested();
                return true;
            }
        }
        if (t == Target::Segment) {
            const Path::SegHit sh = m_edit.nearestOnPath(e.imagePos, tolDoc(ctx));
            if (sh.sub >= 0) {
                const SubPath &sp = m_edit.subPaths[sh.sub];
                const int i0 = sh.seg;
                const int i1 = (sh.seg + 1 < sp.anchors.size()) ? (sh.seg + 1) : 0;
                const int del = (sh.t < 0.5) ? i0 : i1;
                PathEditOps::deleteAnchorAt(m_edit, sh.sub, del);
                if (m_activeSub == sh.sub && m_activeIdx > del)
                    --m_activeIdx;
                else if (m_activeSub == sh.sub && m_activeIdx == del)
                    m_activeIdx = del;
                clampActiveAnchor();
                m_dirtySinceCommit = true;
                if (m_docIndex >= 0)
                    commitEditToDocument(ctx, QObject::tr("删除锚点"));
                emit repaintRequested();
                return true;
            }
        }
    }

    // Alt+点锚点 → 尖角↔平滑（PathEditOps::toggleCorner）
    if (alt && t == Target::Anchor && m_hasEdit) {
        const Path::Hit ah = m_edit.hitTestAnchor(e.imagePos, tolDoc(ctx));
        if (ah.sub >= 0) {
            PathEditOps::toggleCorner(m_edit, ah.sub, ah.index);
            m_dirtySinceCommit = true;
            if (m_docIndex >= 0)
                commitEditToDocument(ctx, QObject::tr("转换锚点"));
            selectAnchor(ah.sub, ah.index);
            emit repaintRequested();
            return true;
        }
    }

    if (t == Target::HandleIn || t == Target::HandleOut) {
        const Path::HandleHit hh = m_edit.hitTestHandle(e.imagePos, tolDoc(ctx), true);
        selectAnchor(hh.sub, hh.index);
        m_activeIsOut = hh.isOut;
        m_state = State::DraggingHandle;
        m_dragStart = e.imagePos;
        return true;
    }

    if (t == Target::Anchor) {
        const Path::Hit ah = m_edit.hitTestAnchor(e.imagePos, tolDoc(ctx));
        selectAnchor(ah.sub, ah.index);
        m_state = State::DraggingAnchor;
        m_dragStart = e.imagePos;
        return true;
    }

    if (t == Target::Segment && ctx.pathAutoAdd) {
        const Path::SegHit sh = m_edit.nearestOnPath(e.imagePos, tolDoc(ctx));
        if (sh.sub >= 0 && PathEditOps::addAnchorAt(m_edit, sh.sub, sh.seg, sh.t)) {
            const int newIdx = sh.seg + 1;
            selectAnchor(sh.sub, newIdx);
            m_state = State::DraggingAnchor;
            m_dragStart = e.imagePos;
            m_dirtySinceCommit = true;
            emit repaintRequested();
            return true;
        }
    }

    // 空白：追加锚点
    appendAnchorAt(e.imagePos);
    if (m_isCurvature) {
        // 弯度钢笔默认不拖控制柄
        m_state = State::Idle;
    } else {
        m_state = State::DraggingOutHandle;
        m_dragStart = e.imagePos;
    }
    emit repaintRequested();
    return true;
}

bool PenTool::mouseMove(const ToolEvent &e, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    m_cursorDoc = e.imagePos;
    const bool alt = e.modifiers.testFlag(Qt::AltModifier);
    const bool shift = e.modifiers.testFlag(Qt::ShiftModifier);

    if (m_state == State::FreeformDrawing && m_hasEdit) {
        ensureEditSub();
        SubPath &sp = m_edit.subPaths[m_activeSub];
        if (!sp.anchors.isEmpty()) {
            const QPointF last = sp.anchors.last().point;
            if (QLineF(last, e.imagePos).length() >= kFreeformStep) {
                appendAnchorAt(e.imagePos);
                m_edit.recomputeAutoHandles(m_activeSub, kCurvatureTension);
            }
        }
        emit repaintRequested();
        return true;
    }

    if (m_state == State::Idle) {
        if (m_hasEdit && ctx.pathRubberBand)
            emit repaintRequested();
        return m_hasEdit;
    }

    if (!m_hasEdit || m_activeSub < 0 || m_activeIdx < 0)
        return false;
    if (m_activeSub >= m_edit.subPaths.size()
        || m_activeIdx >= m_edit.subPaths[m_activeSub].anchors.size()) {
        clampActiveAnchor();
        return false;
    }

    PathAnchor &a = m_edit.subPaths[m_activeSub].anchors[m_activeIdx];
    QPointF pos = e.imagePos;
    if (shift)
        pos = snap45(a.point, pos);

    if (m_state == State::DraggingOutHandle) {
        // PS：按下即拖出对称控制柄（FEATURE_ALIGNED）
        a.outHandle = pos;
        a.outExplicit = true;
        a.inHandle = a.point - (pos - a.point);
        a.inExplicit = true;
        a.corner = false;
        m_dirtySinceCommit = true;
        emit repaintRequested();
        return true;
    }

    if (m_state == State::DraggingAnchor) {
        m_edit.moveAnchor(m_activeSub, m_activeIdx, pos);
        m_dirtySinceCommit = true;
        emit repaintRequested();
        return true;
    }

    if (m_state == State::DraggingHandle) {
        // Alt = FEATURE_NONE 只动一侧；否则 FEATURE_ALIGNED 镜像
        m_edit.setHandle(m_activeSub, m_activeIdx, m_activeIsOut, pos, !alt);
        if (alt)
            m_edit.setCorner(m_activeSub, m_activeIdx, true);
        m_dirtySinceCommit = true;
        emit repaintRequested();
        return true;
    }

    return false;
}

bool PenTool::mouseRelease(const ToolEvent &e, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!e.isLeft())
        return false;

    if (m_state == State::FreeformDrawing) {
        // 自由钢笔松手 = 结束并提交（开路径）
        m_state = State::Idle;
        if (m_hasEdit && m_edit.totalAnchors() >= 2)
            finishPath(ctx, false);
        else
            discardEdit();
        emit repaintRequested();
        return true;
    }

    if (m_state == State::Idle)
        return false;

    // 拖动手柄/锚点松手：若已在文档中则提交一条撤销
    if (m_dirtySinceCommit && m_docIndex >= 0)
        commitEditToDocument(ctx, QObject::tr("编辑路径"));

    m_state = State::Idle;
    emit repaintRequested();
    return true;
}

bool PenTool::wantsShortcutOverride(int key, Qt::KeyboardModifiers mods) const
{
    Q_UNUSED(mods)
    if (!m_hasEdit && m_state == State::Idle)
        return false;
    switch (key) {
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    default:
        return false;
    }
}

bool PenTool::keyPress(int key, Qt::KeyboardModifiers mods, const ToolContext &ctx,
                       ViewPort &view)
{
    Q_UNUSED(mods)
    Q_UNUSED(view)
    if (key == Qt::Key_Escape) {
        // 丢弃未入库会话；已入库的编辑若未提交则重载文档副本
        if (m_docIndex < 0) {
            discardEdit();
        } else {
            loadActiveFromDocument(ctx);
            m_state = State::Idle;
            m_dirtySinceCommit = false;
        }
        emit repaintRequested();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (m_hasEdit)
            finishPath(ctx, true);
        return true;
    }
    return false;
}

void PenTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    // 切换工具：未入库的工作路径丢弃（不压撤销）
    if (m_docIndex < 0)
        discardEdit();
    else {
        m_state = State::Idle;
        // 保留文档路径；清掉本地会话以免脏写
        m_hasEdit = false;
        m_edit = Path{};
        m_docIndex = -1;
    }
}

void PenTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (!m_hasEdit)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 路径本体
    QPainterPath pp = m_edit.toPainterPath();
    QPainterPath wpp;
    for (const SubPath &sp : m_edit.subPaths) {
        if (sp.anchors.isEmpty())
            continue;
        wpp.moveTo(ctx.imageToWidget(sp.anchors.first().point));
        const int sc = sp.segmentCount();
        for (int seg = 0; seg < sc; ++seg) {
            const int i0 = seg;
            const int i1 = (seg + 1 < sp.anchors.size()) ? (seg + 1) : 0;
            const PathAnchor &a = sp.anchors[i0];
            const PathAnchor &b = sp.anchors[i1];
            wpp.cubicTo(ctx.imageToWidget(a.outHandle), ctx.imageToWidget(b.inHandle),
                        ctx.imageToWidget(b.point));
        }
    }
    Q_UNUSED(pp)

    QPen pathPen(Qt::white);
    pathPen.setCosmetic(true);
    pathPen.setWidthF(1.0);
    painter.setPen(pathPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(wpp);
    pathPen.setColor(Qt::black);
    pathPen.setStyle(Qt::DashLine);
    pathPen.setDashPattern({4, 4});
    painter.setPen(pathPen);
    painter.drawPath(wpp);

    // 橡皮带（A18：下标必须先校验）
    if (ctx.pathRubberBand && m_state == State::Idle && m_activeSub >= 0
        && m_activeSub < m_edit.subPaths.size() && m_activeIdx >= 0
        && m_activeIdx < m_edit.subPaths[m_activeSub].anchors.size()
        && !m_edit.subPaths[m_activeSub].closed) {
        const QPointF last = m_edit.subPaths[m_activeSub].anchors[m_activeIdx].point;
        QPen rb(QColor(13, 102, 208));
        rb.setCosmetic(true);
        rb.setWidthF(1.0);
        rb.setStyle(Qt::DashLine);
        painter.setPen(rb);
        painter.drawLine(ctx.imageToWidget(last), ctx.imageToWidget(m_cursorDoc));
    }

    // 锚点与控制柄（命中与绘制同套尺寸常量）
    for (const SubPath &sp : m_edit.subPaths) {
        for (const PathAnchor &a : sp.anchors) {
            const QPointF c = ctx.imageToWidget(a.point);
            if (a.selected) {
                // 控制柄连线 + 圆点
                auto drawHandle = [&](const QPointF &hDoc) {
                    const QPointF h = ctx.imageToWidget(hDoc);
                    QPen hp(Qt::gray);
                    hp.setCosmetic(true);
                    hp.setWidthF(1.0);
                    hp.setStyle(Qt::DashLine);
                    painter.setPen(hp);
                    painter.drawLine(c, h);
                    painter.setBrush(Qt::white);
                    painter.setPen(QPen(Qt::black, 1));
                    painter.drawEllipse(h, kHandleR, kHandleR);
                };
                if (a.hasIn())
                    drawHandle(a.inHandle);
                if (a.hasOut())
                    drawHandle(a.outHandle);
            }
            const QRectF box(c.x() - kAnchorHalf, c.y() - kAnchorHalf, kAnchorHalf * 2,
                             kAnchorHalf * 2);
            painter.setPen(QPen(Qt::black, 1));
            painter.setBrush(a.selected ? QColor(0x0d, 0x66, 0xd0) : Qt::white);
            painter.drawRect(box);
        }
    }

    painter.restore();
}

} // namespace Ps
