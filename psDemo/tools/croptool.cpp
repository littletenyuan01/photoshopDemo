/**
 * croptool.cpp — CropTool 实现（tools 层）。
 *
 * 拖出框 → Enter/双击 → ImageDocument::cropTo；暗化框外区域作预览。
 */
#include "croptool.h"

#include "domain/imagedocument.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

namespace Ps {

CropTool::CropTool(QObject *parent)
    : Tool(Ps::ToolId::Crop, parent)
{
}

Qt::CursorShape CropTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool CropTool::hasOverlay() const
{
    return m_dragging || m_hasFrame;
}

QPointF CropTool::constrainedEnd(const QPointF &start, const QPointF &end, bool square)
{
    if (!square)
        return end;
    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    const qreal side = qMin(qAbs(dx), qAbs(dy));
    const qreal sx = (dx < 0.0) ? -side : side;
    const qreal sy = (dy < 0.0) ? -side : side;
    return QPointF(start.x() + sx, start.y() + sy);
}

QRectF CropTool::currentImageRect() const
{
    return QRectF(m_startImage,
                  constrainedEnd(m_startImage, m_endImage, m_constrainSquare)).normalized();
}

QRectF CropTool::currentWidgetRect() const
{
    return QRectF(m_startWidget,
                  constrainedEnd(m_startWidget, m_endWidget, m_constrainSquare)).normalized();
}

void CropTool::clearFrame()
{
    m_dragging = false;
    m_hasFrame = false;
    m_constrainSquare = false;
}

bool CropTool::commit(const ToolContext &ctx)
{
    if (!ctx.document || !m_hasFrame)
        return false;

    const QRectF rf = currentImageRect();
    QRect rect(qFloor(rf.left()), qFloor(rf.top()),
               qCeil(rf.right()) - qFloor(rf.left()),
               qCeil(rf.bottom()) - qFloor(rf.top()));
    rect = rect.intersected(QRect(0, 0, ctx.document->width(), ctx.document->height()));
    if (rect.width() < 1 || rect.height() < 1) {
        clearFrame();
        emit repaintRequested();
        return true;
    }

    ctx.document->cropTo(rect);
    clearFrame();
    emit repaintRequested();
    return true;
}

void CropTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (!m_dragging && !m_hasFrame)
        return;

    const QRectF r = currentWidgetRect();
    if (r.width() < 0.5 && r.height() < 0.5)
        return;

    painter.save();

    // 框外半透明遮罩（对齐 PS/GIMP 裁剪预览）
    if (ctx.document && ctx.viewZoom > 0.0) {
        const QRectF docWidget(
            ctx.imageToWidget(QPointF(0, 0)),
            ctx.imageToWidget(QPointF(ctx.document->width(), ctx.document->height())));
        QPainterPath dim;
        dim.addRect(docWidget);
        QPainterPath hole;
        hole.addRect(r);
        dim = dim.subtracted(hole);
        painter.fillPath(dim, QColor(0, 0, 0, 120));
    }

    QPen pen(Qt::white);
    pen.setCosmetic(true);
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({4, 4});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(r);

    pen.setColor(Qt::black);
    pen.setDashOffset(4);
    painter.setPen(pen);
    painter.drawRect(r.adjusted(1, 1, -1, -1));

    painter.restore();
}

bool CropTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    // 双击：有框则确认裁剪（对照 GIMP Confirm）
    if (event.doubleClick) {
        if (m_hasFrame)
            commit(ctx);
        return true;
    }

    m_dragging = true;
    m_hasFrame = false;
    m_constrainSquare = event.modifiers.testFlag(Qt::ShiftModifier);
    m_startImage = event.imagePos;
    m_endImage = event.imagePos;
    m_startWidget = event.widgetPos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool CropTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    m_constrainSquare = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    emit repaintRequested();
    return true;
}

bool CropTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;

    m_constrainSquare = event.modifiers.testFlag(Qt::ShiftModifier);
    m_endImage = event.imagePos;
    m_endWidget = event.widgetPos;
    m_dragging = false;

    const QRectF rf = currentImageRect();
    if (rf.width() >= 1.0 && rf.height() >= 1.0)
        m_hasFrame = true;
    else
        clearFrame();

    emit repaintRequested();
    return true;
}

bool CropTool::wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const
{
    Q_UNUSED(modifiers)
    if (!m_hasFrame && !m_dragging)
        return false;
    return key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape;
}

bool CropTool::keyPress(int key, Qt::KeyboardModifiers modifiers,
                        const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(modifiers)
    Q_UNUSED(view)
    if (key == Qt::Key_Escape) {
        if (!m_hasFrame && !m_dragging)
            return false;
        clearFrame();
        emit repaintRequested();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        return commit(ctx);
    }
    return false;
}

void CropTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_hasFrame && !m_dragging)
        return;
    clearFrame();
    emit repaintRequested();
}

} // namespace Ps
