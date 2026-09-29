/**
 * polygonallassotool.cpp — PolygonalLassoTool 实现（tools 层）。
 *
 * 【对照 GIMP】gimppolygonselecttool.c（key_press / 折线 widget）+
 * gimp_channel_select_polygon；写入仍走 PaintEngine::selectPolygon。
 * Shift 吸附对齐 PS 多边形套索（45°）并额外吸附到前一边的平行/垂线。
 */
#include "polygonallassotool.h"

#include "domain/imagedocument.h"

#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

#include <cmath>

namespace Ps {

namespace {
/** 点近起点则闭合的热区半径（控件像素）。 */
constexpr qreal kCloseWidgetRadius = 10.0;

/** 两角之差的最小绝对值（弧度，折叠到 (-π, π]）。 */
qreal angleDelta(qreal a, qreal b)
{
    qreal d = a - b;
    while (d > M_PI)
        d -= 2.0 * M_PI;
    while (d < -M_PI)
        d += 2.0 * M_PI;
    return d;
}
} // namespace

PolygonalLassoTool::PolygonalLassoTool(QObject *parent)
    : Tool(Ps::ToolId::PolygonalLasso, parent)
{
}

Qt::CursorShape PolygonalLassoTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool PolygonalLassoTool::hasOverlay() const
{
    return m_active && !m_widgetPts.isEmpty();
}

ChannelOp PolygonalLassoTool::opFromModifiers(Qt::KeyboardModifiers modifiers)
{
    // 仅首点锁定运算；拖中 Shift 改作角度吸附，不再改 ChannelOp
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    if (shift && ctrl)
        return ChannelOp::Intersect;
    if (shift)
        return ChannelOp::Add;
    if (ctrl)
        return ChannelOp::Subtract;
    return ChannelOp::Replace;
}

QPointF PolygonalLassoTool::constrainEnd(const QPointF &origin, const QPointF &cursor,
                                         const QPointF *prevOrigin, bool shift)
{
    if (!shift)
        return cursor;

    const qreal dx = cursor.x() - origin.x();
    const qreal dy = cursor.y() - origin.y();
    const qreal len = std::hypot(dx, dy);
    if (len < 1e-6)
        return cursor;

    const qreal ang = std::atan2(dy, dx);

    // 候选角：轴对齐 45° 步进（水平/垂直/对角，对齐 PS）
    QVector<qreal> cands;
    cands.reserve(16);
    for (int i = 0; i < 8; ++i)
        cands.append(i * (M_PI / 4.0));

    // 相对前一边：平行与垂直（用户点名的「垂直于前一条边」）
    if (prevOrigin) {
        const qreal pdx = origin.x() - prevOrigin->x();
        const qreal pdy = origin.y() - prevOrigin->y();
        if (std::hypot(pdx, pdy) > 1e-6) {
            const qreal prevAng = std::atan2(pdy, pdx);
            for (int k = 0; k < 4; ++k)
                cands.append(prevAng + k * (M_PI / 2.0));
        }
    }

    qreal best = cands.first();
    qreal bestAbs = std::fabs(angleDelta(ang, best));
    for (int i = 1; i < cands.size(); ++i) {
        const qreal d = std::fabs(angleDelta(ang, cands[i]));
        if (d < bestAbs) {
            bestAbs = d;
            best = cands[i];
        }
    }

    return QPointF(origin.x() + len * std::cos(best),
                   origin.y() + len * std::sin(best));
}

void PolygonalLassoTool::updateCursor(const QPointF &imagePos, const QPointF &widgetPos,
                                      bool shift)
{
    if (m_imagePts.isEmpty()) {
        m_cursorImage = imagePos;
        m_cursorWidget = widgetPos;
        return;
    }

    const QPointF *prevImg = (m_imagePts.size() >= 2)
                                 ? &m_imagePts[m_imagePts.size() - 2]
                                 : nullptr;
    const QPointF *prevWid = (m_widgetPts.size() >= 2)
                                 ? &m_widgetPts[m_widgetPts.size() - 2]
                                 : nullptr;

    m_cursorImage = constrainEnd(m_imagePts.last(), imagePos, prevImg, shift);
    m_cursorWidget = constrainEnd(m_widgetPts.last(), widgetPos, prevWid, shift);
}

bool PolygonalLassoTool::nearFirstWidget(const QPointF &widgetPos) const
{
    if (m_widgetPts.size() < 3)
        return false;
    const QPointF d = widgetPos - m_widgetPts.first();
    return d.x() * d.x() + d.y() * d.y() <= kCloseWidgetRadius * kCloseWidgetRadius;
}

void PolygonalLassoTool::appendVertex(const QPointF &imagePos, const QPointF &widgetPos)
{
    m_imagePts.append(imagePos);
    m_widgetPts.append(widgetPos);
}

void PolygonalLassoTool::clearPath()
{
    m_active = false;
    m_imagePts.clear();
    m_widgetPts.clear();
}

bool PolygonalLassoTool::commit(const ToolContext &ctx)
{
    if (!ctx.document || m_imagePts.size() < 3)
        return false;

    const QPolygonF poly(m_imagePts);
    const QRectF br = poly.boundingRect();
    if (br.width() < 0.5 && br.height() < 0.5) {
        clearPath();
        emit repaintRequested();
        return true;
    }

    ctx.document->selectPolygon(poly, m_op);
    clearPath();
    emit repaintRequested();
    return true;
}

QPointF PolygonalLassoTool::rubberWidgetEnd() const
{
    if (nearFirstWidget(m_cursorWidget))
        return m_widgetPts.first();
    return m_cursorWidget;
}

void PolygonalLassoTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    Q_UNUSED(ctx)
    if (m_widgetPts.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto stroke = [&](const QColor &color, qreal dashOffset) {
        QPen pen(color);
        pen.setCosmetic(true);
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({4, 4});
        pen.setDashOffset(dashOffset);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if (m_widgetPts.size() >= 2)
            painter.drawPolyline(m_widgetPts.constData(), m_widgetPts.size());
        painter.drawLine(m_widgetPts.last(), rubberWidgetEnd());
    };
    stroke(Qt::white, 0);
    stroke(Qt::black, 4);

    if (m_widgetPts.size() >= 3) {
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(m_widgetPts.first(), 4, 4);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(m_widgetPts.first(), 3, 3);
    }

    painter.restore();
}

bool PolygonalLassoTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    const bool shift = event.modifiers.testFlag(Qt::ShiftModifier);

    // 双击：用当前吸附后的橡皮筋点闭合
    if (event.doubleClick) {
        if (!m_active)
            return true;
        updateCursor(event.imagePos, event.widgetPos, shift);
        if (!nearFirstWidget(m_cursorWidget))
            appendVertex(m_cursorImage, m_cursorWidget);
        commit(ctx);
        return true;
    }

    if (!m_active) {
        m_active = true;
        m_op = opFromModifiers(event.modifiers);
        m_imagePts.clear();
        m_widgetPts.clear();
        appendVertex(event.imagePos, event.widgetPos);
        m_cursorImage = event.imagePos;
        m_cursorWidget = event.widgetPos;
        emit repaintRequested();
        return true;
    }

    updateCursor(event.imagePos, event.widgetPos, shift);

    if (nearFirstWidget(m_cursorWidget)) {
        commit(ctx);
        return true;
    }

    // 落点用吸附后的坐标（按住 Shift 时水平/垂直/垂线对齐）
    appendVertex(m_cursorImage, m_cursorWidget);
    emit repaintRequested();
    return true;
}

bool PolygonalLassoTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_active)
        return false;

    updateCursor(event.imagePos, event.widgetPos,
                 event.modifiers.testFlag(Qt::ShiftModifier));
    emit repaintRequested();
    return true;
}

bool PolygonalLassoTool::wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const
{
    Q_UNUSED(modifiers)
    if (!m_active)
        return false;
    // 抢走菜单 Delete=清除，以及 Enter 等，避免编辑中途误触
    switch (key) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Escape:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return true;
    default:
        return false;
    }
}

bool PolygonalLassoTool::keyPress(int key, Qt::KeyboardModifiers modifiers,
                                  const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(modifiers)
    Q_UNUSED(view)
    if (!m_active)
        return false;

    if (key == Qt::Key_Escape) {
        clearPath();
        emit repaintRequested();
        return true;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        // 回退到上一个顶点；删光则退出编辑
        if (!m_imagePts.isEmpty()) {
            m_imagePts.removeLast();
            m_widgetPts.removeLast();
        }
        if (m_imagePts.isEmpty())
            clearPath();
        emit repaintRequested();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        commit(ctx);
        return true;
    }
    return false;
}

void PolygonalLassoTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_active)
        return;
    clearPath();
    emit repaintRequested();
}

} // namespace Ps
