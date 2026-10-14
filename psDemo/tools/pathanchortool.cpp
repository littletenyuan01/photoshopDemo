/**
 * pathanchortool.cpp — 添加/删除/转换锚点工具实现。
 */
#include "pathanchortool.h"

#include "domain/imagedocument.h"
#include "patheditops.h"

#include <QPainter>
#include <QPainterPath>

namespace Ps {

namespace {
constexpr qreal kTolWidget = 8.0;
constexpr qreal kAnchorHalf = 3.5;
} // namespace

PathAnchorTool::PathAnchorTool(Ps::ToolId id, QObject *parent)
    : Tool(id, parent)
{
    switch (id) {
    case Ps::ToolId::DeleteAnchorPoint:
        m_mode = Mode::Delete;
        break;
    case Ps::ToolId::ConvertPoint:
        m_mode = Mode::Convert;
        break;
    default:
        m_mode = Mode::Add;
        break;
    }
}

Qt::CursorShape PathAnchorTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool PathAnchorTool::hasOverlay() const
{
    // 始终允许 drawOverlay 自检文档真值，避免删路径后幽灵残留（A36）
    return true;
}

qreal PathAnchorTool::tolDoc(const ToolContext &ctx) const
{
    return kTolWidget / qMax(ctx.viewZoom, 1e-6);
}

bool PathAnchorTool::loadActive(const ToolContext &ctx)
{
    if (!ctx.document)
        return false;
    Path *p = ctx.document->activePath();
    if (!p || p->isEmpty()) {
        m_hasPath = false;
        m_docIndex = -1;
        return false;
    }
    m_path = *p;
    m_docIndex = ctx.document->activePathIndex();
    m_hasPath = true;
    return true;
}

/** 叠加用：以文档路径栈为准（可见且仍存在），不画幽灵副本。 */
const Path *PathAnchorTool::livePathForOverlay(const ToolContext &ctx) const
{
    if (!ctx.document || m_docIndex < 0)
        return nullptr;
    const Path *p = ctx.document->paths().pathAt(m_docIndex);
    if (!p || p->isEmpty() || !p->visible)
        return nullptr;
    return p;
}

void PathAnchorTool::commit(const ToolContext &ctx, const QString &label)
{
    if (!ctx.document || m_docIndex < 0)
        return;
    ctx.document->pushPathUndo(label);
    ctx.document->setPathAt(m_docIndex, m_path);
    emit repaintRequested();
}

bool PathAnchorTool::mousePress(const ToolEvent &e, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!e.isLeft() || !ctx.document)
        return false;
    if (!loadActive(ctx))
        return true; // 消费点击，避免落到别的默认行为

    const qreal tol = tolDoc(ctx);

    if (m_mode == Mode::Add) {
        const Path::SegHit sh = m_path.nearestOnPath(e.imagePos, tol);
        if (sh.sub < 0)
            return true;
        if (PathEditOps::addAnchorAt(m_path, sh.sub, sh.seg, sh.t))
            commit(ctx, QObject::tr("添加锚点"));
        return true;
    }

    if (m_mode == Mode::Delete) {
        const Path::Hit ah = m_path.hitTestAnchor(e.imagePos, tol);
        if (ah.sub >= 0) {
            if (PathEditOps::deleteAnchorAt(m_path, ah.sub, ah.index))
                commit(ctx, QObject::tr("删除锚点"));
            return true;
        }
        // 点在线段上：删近端锚点（与钢笔 Ctrl+线段同口径）
        const Path::SegHit sh = m_path.nearestOnPath(e.imagePos, tol);
        if (sh.sub >= 0) {
            const SubPath &sp = m_path.subPaths[sh.sub];
            const int i0 = sh.seg;
            const int i1 = (sh.seg + 1 < sp.anchors.size()) ? (sh.seg + 1) : 0;
            const int del = (sh.t < 0.5) ? i0 : i1;
            if (PathEditOps::deleteAnchorAt(m_path, sh.sub, del))
                commit(ctx, QObject::tr("删除锚点"));
        }
        return true;
    }

    // Convert
    const Path::Hit ah = m_path.hitTestAnchor(e.imagePos, tol);
    if (ah.sub >= 0) {
        if (PathEditOps::toggleCorner(m_path, ah.sub, ah.index))
            commit(ctx, QObject::tr("转换锚点"));
    }
    return true;
}

void PathAnchorTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    const Path *live = livePathForOverlay(ctx);
    if (!live)
        return;
    const Path &path = *live; // 始终画文档真值，避免删/撤后幽灵（A36）

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath wpp;
    for (const SubPath &sp : path.subPaths) {
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
    QPen pathPen(Qt::white);
    pathPen.setCosmetic(true);
    pathPen.setWidthF(1.0);
    painter.setPen(pathPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(wpp);
    pathPen.setColor(Qt::black);
    pathPen.setStyle(Qt::DashLine);
    painter.setPen(pathPen);
    painter.drawPath(wpp);

    for (const SubPath &sp : path.subPaths) {
        for (const PathAnchor &a : sp.anchors) {
            const QPointF c = ctx.imageToWidget(a.point);
            const QRectF box(c.x() - kAnchorHalf, c.y() - kAnchorHalf, kAnchorHalf * 2,
                             kAnchorHalf * 2);
            painter.setPen(QPen(Qt::black, 1));
            painter.setBrush(a.corner ? QColor(0xd0, 0x66, 0x0d) : Qt::white);
            painter.drawRect(box);
        }
    }
    painter.restore();
}

} // namespace Ps
