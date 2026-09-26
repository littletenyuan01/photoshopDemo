#include "canvasview.h"

#include "domain/imagedocument.h"
#include "engine/compositor.h"
#include "tools/handtool.h"
#include "tools/tool.h"
#include "tools/toolcontext.h"
#include "tools/toolevent.h"
#include "tools/toolmanager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
    , m_toolManager(new Ps::ToolManager(this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);

    // 工具的请求信号统一由管理器转发上来，此处只连一次
    connect(m_toolManager, &Ps::ToolManager::repaintRequested,
            this, qOverload<>(&QWidget::update));
    connect(m_toolManager, &Ps::ToolManager::cursorChangeRequested,
            this, [this](Qt::CursorShape shape) { setCursor(shape); });

    refreshToolContext();
    updateToolCursor();
}

CanvasView::~CanvasView() = default;

void CanvasView::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;

    if (m_document) {
        // 只订阅「像素变了」与「结构变了」：
        // 图层属性变化（显隐/透明度）也走 contentChanged，画布需重合成，
        // 但那是 Compositor 的事，画布不必区分。
        connect(m_document, &Ps::ImageDocument::contentChanged, this, [this]() {
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

    refreshToolContext();
}

// —— 几何与钳制 ——

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
        // 大于视口：左缘 ∈ [vw-contentW, 0]
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

// —— 缩放 ——

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

void CanvasView::zoomAt(const QPointF &widgetPos, qreal factor)
{
    if (!m_document)
        return;

    // 锚点缩放：让 widgetPos 下的图像点缩放前后保持在同一屏幕位置。
    // 这段数学此前在 wheelEvent / 缩放工具里各写了一遍，现收敛于此。
    const QPointF before = (widgetPos - m_offset) / m_zoom;
    m_zoom = qBound(0.05, m_zoom * factor, 32.0);
    m_offset = widgetPos - before * m_zoom;
    clampOffset();
    update();
    notifyViewChanged();
}

void CanvasView::panBy(const QPointF &deltaWidget)
{
    if (!m_document)
        return;
    m_offset += deltaWidget;
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

// —— 工具与参数 ——

void CanvasView::refreshToolContext()
{
    m_toolContext.document = m_document;
    m_toolContext.foreground = m_fg;
    m_toolContext.background = m_bg;
    m_toolContext.brushRadius = m_brushRadius;

    if (m_toolManager)
        m_toolManager->setContext(m_toolContext);
}

void CanvasView::setCurrentTool(Ps::ToolId id)
{
    if (!m_toolManager)
        return;
    // 切换动作交给管理器：它会 deactivate 旧工具（清理拖拽态）再激活新工具
    m_toolManager->setActiveTool(id, *this);
    updateToolCursor();
}

Ps::ToolId CanvasView::currentTool() const
{
    return m_toolManager ? m_toolManager->activeToolId() : Ps::ToolId::Move;
}

void CanvasView::setForegroundColor(const QColor &c)
{
    if (!c.isValid())
        return;
    m_fg = c;
    refreshToolContext();
}

void CanvasView::setBackgroundColor(const QColor &c)
{
    if (!c.isValid())
        return;
    m_bg = c;
    refreshToolContext();
}

void CanvasView::setBrushDiameter(int diameter)
{
    m_brushRadius = qMax(0.5, diameter * 0.5);
    refreshToolContext();
}

int CanvasView::brushDiameter() const
{
    return qRound(m_brushRadius * 2.0);
}

void CanvasView::updateToolCursor()
{
    if (m_toolManager)
        setCursor(m_toolManager->activeCursorShape());
}

// —— 绘制 ——

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

    // 工具浮层最后画：位于图像之上（对应 GIMP display 的 tool_items / preview_items）
    Ps::Tool *tool = m_toolManager ? m_toolManager->activeTool() : nullptr;
    if (tool && tool->hasOverlay()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        tool->drawOverlay(painter, m_toolContext);
    }
}

// —— 事件 ——

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (!m_document) {
        QWidget::wheelEvent(event);
        return;
    }
    const qreal factor = event->angleDelta().y() > 0 ? 1.1 : (1.0 / 1.1);
    zoomAt(event->position(), factor);
    event->accept();
}

Ps::ToolEvent CanvasView::makeToolEvent(QMouseEvent *event) const
{
    Ps::ToolEvent e;
    e.widgetPos = event->position();
    e.imagePos = widgetToImage(e.widgetPos);
    e.button = event->button();
    e.buttons = event->buttons();
    e.modifiers = event->modifiers();
    return e;
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    if (!m_document || !m_toolManager) {
        QWidget::mousePressEvent(event);
        return;
    }

    const Ps::ToolEvent e = makeToolEvent(event);

    // 通用平移手势（中键 / Alt+左键）优先于当前工具：由抓手工具承担。
    // 对齐 PS/GIMP：任何工具下都能临时平移。
    if (Ps::HandTool::isPanGesture(e)) {
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            hand->setContext(m_toolContext);
            m_panning = hand->mousePress(e, m_toolContext, *this);
            if (m_panning) {
                event->accept();
                return;
            }
        }
    }

    // 其余事件交给活动工具；未消费则视为无操作（不再有工具专属 if 分支）
    if (m_toolManager->dispatchPress(e, *this)) {
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_document)
        emit cursorImagePosChanged(widgetToImage(event->position()), true);

    if (!m_document || !m_toolManager) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const Ps::ToolEvent e = makeToolEvent(event);

    // 平移进行中：固定发给抓手工具，不因中途切工具而丢失 release
    if (m_panning) {
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            hand->mouseMove(e, m_toolContext, *this);
            event->accept();
            return;
        }
    }

    if (m_toolManager->dispatchMove(e, *this)) {
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_document || !m_toolManager) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const Ps::ToolEvent e = makeToolEvent(event);

    if (m_panning) {
        m_panning = false;
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            hand->mouseRelease(e, m_toolContext, *this);
            // 平移结束后恢复当前工具的光标
            updateToolCursor();
            event->accept();
            return;
        }
    }

    if (m_toolManager->dispatchRelease(e, *this)) {
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

// —— 内部工具 ——

void CanvasView::rebuildCache()
{
    if (!m_document) {
        m_cache = QImage();
        return;
    }
    // 目前仍是全量重合成；脏区局部重算见 docs/architecture.md §8.1 主线 ②
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
