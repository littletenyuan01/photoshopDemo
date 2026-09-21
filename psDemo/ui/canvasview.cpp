#include "canvasview.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"

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
}

void CanvasView::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document) {
        // 断开旧文档全部信号，避免悬空连接
        disconnect(m_document, nullptr, this, nullptr);
    }

    m_document = document;

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
        zoomFit();
    } else {
        m_cache = QImage();
        update();
    }
}

void CanvasView::setZoom(qreal zoom)
{
    m_zoom = qBound(0.05, zoom, 32.0);
    update();
}

void CanvasView::zoomFit()
{
    if (!m_document || m_document->width() <= 0 || m_document->height() <= 0) {
        m_zoom = 1.0;
        m_offset = QPointF();
        update();
        return;
    }

    const qreal margin = 24.0;
    const qreal sx = (width() - margin * 2) / m_document->width();
    const qreal sy = (height() - margin * 2) / m_document->height();
    m_zoom = qBound(0.05, qMin(sx, sy), 32.0);

    // 居中放置
    const QSizeF size(m_document->width() * m_zoom, m_document->height() * m_zoom);
    m_offset = QPointF((width() - size.width()) * 0.5, (height() - size.height()) * 0.5);
    update();
}

void CanvasView::zoomActual()
{
    if (!m_document)
        return;
    m_zoom = 1.0;
    const QSizeF size(m_document->width() * m_zoom, m_document->height() * m_zoom);
    m_offset = QPointF((width() - size.width()) * 0.5, (height() - size.height()) * 0.5);
    update();
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
    // 放大很多时关掉平滑，避免像素发糊，便于「像素级」观察
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 4.0);
    painter.drawImage(target, m_cache);
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (!m_document) {
        QWidget::wheelEvent(event);
        return;
    }

    // 以鼠标位置为缩放锚点：缩放前后该图像点仍对准同一屏幕点
    const QPointF mouse = event->position();
    const QPointF before = (mouse - m_offset) / m_zoom;

    const qreal factor = event->angleDelta().y() > 0 ? 1.1 : (1.0 / 1.1);
    setZoom(m_zoom * factor);

    m_offset = mouse - before * m_zoom;
    update();
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    // 中键，或 Alt+左键：平移画布（不占用将来画笔的左键）
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        m_offset += delta;
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void CanvasView::rebuildCache()
{
    if (!m_document) {
        m_cache = QImage();
        return;
    }
    // 视图不直接读各层像素，统一走 Compositor
    m_cache = Ps::Compositor::composite(*m_document);
}

QPointF CanvasView::imageToWidget(const QPointF &imagePos) const
{
    return m_offset + imagePos * m_zoom;
}

QRectF CanvasView::imageRectInWidget() const
{
    if (!m_document)
        return {};
    return QRectF(m_offset, QSizeF(m_document->width() * m_zoom, m_document->height() * m_zoom));
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
