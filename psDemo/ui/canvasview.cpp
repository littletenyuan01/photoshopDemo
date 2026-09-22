#include "canvasview.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/paintengine.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);
    updateToolCursor();
}

void CanvasView::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;
    m_painting = false;

    if (m_document) {
        connect(m_document, &Ps::ImageDocument::documentChanged, this, [this]() {
            rebuildCache();
            update();
        });
        connect(m_document, &Ps::ImageDocument::structureChanged, this, [this]() {
            rebuildCache();
            update();
        });
        rebuildCache();
        if (width() > 50 && height() > 50)
            zoomFit();
        else
            m_pendingFit = true;
    } else {
        m_cache = QImage();
        m_pendingFit = false;
        update();
        notifyViewChanged();
    }
}

QSizeF CanvasView::contentSize() const
{
    if (!m_document)
        return {};
    return QSizeF(m_document->width() * m_zoom, m_document->height() * m_zoom);
}

void CanvasView::clampOffset()
{
    if (!m_document) {
        m_offset = QPointF();
        return;
    }

    const QSizeF content = contentSize();
    const qreal vw = width();
    const qreal vh = height();

    // 小于视口：强制居中，禁止拖出窗口
    if (content.width() <= vw)
        m_offset.setX((vw - content.width()) * 0.5);
    else
        // 大于视口：左缘 ∈ [vw-contentW, 0]，右缘始终不离开视口右侧以外的空洞无限拖
        m_offset.setX(qBound(vw - content.width(), m_offset.x(), 0.0));

    if (content.height() <= vh)
        m_offset.setY((vh - content.height()) * 0.5);
    else
        m_offset.setY(qBound(vh - content.height(), m_offset.y(), 0.0));
}

int CanvasView::scrollMaxX() const
{
    const qreal extra = contentSize().width() - width();
    return extra > 0.5 ? qCeil(extra) : 0;
}

int CanvasView::scrollMaxY() const
{
    const qreal extra = contentSize().height() - height();
    return extra > 0.5 ? qCeil(extra) : 0;
}

int CanvasView::scrollX() const
{
    if (scrollMaxX() <= 0)
        return 0;
    // offset.x 为负或较小表示向右看了更多内容；scroll = -offset.x（左对齐时 0）
    return qBound(0, qRound(-m_offset.x()), scrollMaxX());
}

int CanvasView::scrollY() const
{
    if (scrollMaxY() <= 0)
        return 0;
    return qBound(0, qRound(-m_offset.y()), scrollMaxY());
}

void CanvasView::setScrollOffset(int scrollX, int scrollY)
{
    if (!m_document)
        return;

    const QSizeF content = contentSize();
    if (content.width() <= width())
        m_offset.setX((width() - content.width()) * 0.5);
    else
        m_offset.setX(-qreal(qBound(0, scrollX, scrollMaxX())));

    if (content.height() <= height())
        m_offset.setY((height() - content.height()) * 0.5);
    else
        m_offset.setY(-qreal(qBound(0, scrollY, scrollMaxY())));

    clampOffset();
    update();
    notifyViewChanged();
}

void CanvasView::setZoom(qreal zoom)
{
    const QPointF anchorWidget(width() * 0.5, height() * 0.5);
    const QPointF anchorImage = widgetToImage(anchorWidget);
    m_zoom = qBound(0.05, zoom, 32.0);
    m_offset = anchorWidget - anchorImage * m_zoom;
    clampOffset();
    update();
    notifyViewChanged();
}

void CanvasView::zoomFit()
{
    m_pendingFit = false;
    if (!m_document || m_document->width() <= 0 || m_document->height() <= 0) {
        m_zoom = 1.0;
        m_offset = QPointF();
        update();
        notifyViewChanged();
        return;
    }

    const qreal margin = 24.0;
    const qreal sx = (width() - margin * 2) / m_document->width();
    const qreal sy = (height() - margin * 2) / m_document->height();
    m_zoom = qBound(0.05, qMin(sx, sy), 32.0);
    centerOnImage();
}

void CanvasView::zoomActual()
{
    m_pendingFit = false;
    if (!m_document)
        return;
    m_zoom = 1.0;
    centerOnImage();
}

void CanvasView::centerOnImage()
{
    if (!m_document) {
        m_offset = QPointF();
        update();
        notifyViewChanged();
        return;
    }
    const QSizeF size = contentSize();
    m_offset = QPointF((width() - size.width()) * 0.5, (height() - size.height()) * 0.5);
    clampOffset();
    update();
    notifyViewChanged();
}

void CanvasView::setCurrentTool(Ps::ToolId id)
{
    if (m_tool == id)
        return;
    m_tool = id;
    m_painting = false;
    updateToolCursor();
}

void CanvasView::setForegroundColor(const QColor &c)
{
    if (c.isValid())
        m_fg = c;
}

void CanvasView::setBackgroundColor(const QColor &c)
{
    if (c.isValid())
        m_bg = c;
}

void CanvasView::setBrushDiameter(int diameter)
{
    m_brushRadius = qMax(0.5, diameter * 0.5);
}

void CanvasView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(45, 45, 48));

    if (!m_document || m_cache.isNull()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, tr("无文档 — 请新建或打开图像"));
        return;
    }

    const QRectF target = imageRectInWidget();
    drawCheckerboard(painter, target.toAlignedRect());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 4.0);
    painter.drawImage(target, m_cache);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (!m_document) {
        QWidget::wheelEvent(event);
        return;
    }

    const QPointF mouse = event->position();
    const QPointF before = (mouse - m_offset) / m_zoom;

    const qreal factor = event->angleDelta().y() > 0 ? 1.1 : (1.0 / 1.1);
    m_zoom = qBound(0.05, m_zoom * factor, 32.0);
    m_offset = mouse - before * m_zoom;
    clampOffset();
    update();
    notifyViewChanged();
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_document) {
        if (m_tool == Ps::ToolId::Hand) {
            m_panning = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        if (m_tool == Ps::ToolId::Zoom) {
            const QPointF mouse = event->position();
            const QPointF before = (mouse - m_offset) / m_zoom;
            m_zoom = qBound(0.05, m_zoom * 1.25, 32.0);
            m_offset = mouse - before * m_zoom;
            clampOffset();
            update();
            notifyViewChanged();
            event->accept();
            return;
        }

        if (isPaintTool()) {
            beginPaintStroke(widgetToImage(event->position()));
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::RightButton && m_tool == Ps::ToolId::Zoom && m_document) {
        const QPointF mouse = event->position();
        const QPointF before = (mouse - m_offset) / m_zoom;
        m_zoom = qBound(0.05, m_zoom / 1.25, 32.0);
        m_offset = mouse - before * m_zoom;
        clampOffset();
        update();
        notifyViewChanged();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_document)
        emit cursorImagePosChanged(widgetToImage(event->position()), true);

    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        m_offset += delta;
        clampOffset();
        update();
        notifyViewChanged();
        event->accept();
        return;
    }

    if (m_painting && (event->buttons() & Qt::LeftButton)) {
        continuePaintStroke(widgetToImage(event->position()));
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        updateToolCursor();
        event->accept();
        return;
    }

    if (m_painting && event->button() == Qt::LeftButton) {
        endPaintStroke();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pendingFit && width() > 50 && height() > 50) {
        zoomFit();
    } else {
        clampOffset();
        update();
        notifyViewChanged();
    }
}

void CanvasView::leaveEvent(QEvent *event)
{
    emit cursorImagePosChanged(QPointF(), false);
    QWidget::leaveEvent(event);
}

void CanvasView::rebuildCache()
{
    if (!m_document) {
        m_cache = QImage();
        return;
    }
    m_cache = Ps::Compositor::composite(*m_document);
}

void CanvasView::notifyViewChanged()
{
    emit viewChanged();
}

QPointF CanvasView::imageToWidget(const QPointF &imagePos) const
{
    return m_offset + imagePos * m_zoom;
}

QPointF CanvasView::widgetToImage(const QPointF &widgetPos) const
{
    return (widgetPos - m_offset) / m_zoom;
}

QRectF CanvasView::imageRectInWidget() const
{
    if (!m_document)
        return {};
    return QRectF(m_offset, contentSize());
}

void CanvasView::drawCheckerboard(QPainter &painter, const QRect &rect) const
{
    const int cell = 8;
    for (int y = rect.top(); y < rect.bottom(); y += cell) {
        for (int x = rect.left(); x < rect.right(); x += cell) {
            const bool light = ((x / cell) + (y / cell)) % 2 == 0;
            painter.fillRect(QRect(x, y, cell, cell).intersected(rect),
                             light ? QColor(255, 255, 255) : QColor(200, 200, 200));
        }
    }
}

void CanvasView::updateToolCursor()
{
    switch (m_tool) {
    case Ps::ToolId::Hand:
        setCursor(Qt::OpenHandCursor);
        break;
    case Ps::ToolId::Zoom:
        setCursor(Qt::CrossCursor);
        break;
    case Ps::ToolId::Brush:
    case Ps::ToolId::Eraser:
        setCursor(Qt::CrossCursor);
        break;
    default:
        unsetCursor();
        break;
    }
}

bool CanvasView::isPaintTool() const
{
    return m_tool == Ps::ToolId::Brush || m_tool == Ps::ToolId::Eraser;
}

void CanvasView::beginPaintStroke(const QPointF &imagePos)
{
    Ps::Layer *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer || !layer->isVisible())
        return;

    m_painting = true;
    m_lastPaintPos = imagePos;

    const auto mode = (m_tool == Ps::ToolId::Eraser)
                          ? Ps::PaintEngine::Mode::Erase
                          : Ps::PaintEngine::Mode::Paint;
    Ps::PaintEngine::stampDab(layer->pixels(), imagePos, m_brushRadius, m_fg, mode);
    m_document->markDirty();
}

void CanvasView::continuePaintStroke(const QPointF &imagePos)
{
    Ps::Layer *layer = m_document ? m_document->activeLayer() : nullptr;
    if (!layer || !m_painting)
        return;

    const auto mode = (m_tool == Ps::ToolId::Eraser)
                          ? Ps::PaintEngine::Mode::Erase
                          : Ps::PaintEngine::Mode::Paint;
    m_lastPaintPos = Ps::PaintEngine::strokeSegment(
        layer->pixels(), m_lastPaintPos, imagePos, m_brushRadius, m_fg, mode);
    m_document->markDirty();
}

void CanvasView::endPaintStroke()
{
    m_painting = false;
}
