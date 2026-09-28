#include "canvasview.h"

#include "domain/imagedocument.h"
#include "domain/selection.h"
#include "engine/compositor.h"
#include "pixmaputils.h"
#include "tools/handtool.h"
#include "tools/tool.h"
#include "tools/toolcontext.h"
#include "tools/toolevent.h"
#include "tools/toolmanager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

CanvasView::CanvasView(QWidget *parent)
    : QWidget(parent)
    , m_toolManager(new Ps::ToolManager(this))
    , m_antsTimer(new QTimer(this))
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // minimumSize 由 canvasworkspace.ui 声明
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);

    // 工具的请求信号统一由管理器转发上来，此处只连一次
    connect(m_toolManager, &Ps::ToolManager::repaintRequested,
            this, qOverload<>(&QWidget::update));
    connect(m_toolManager, &Ps::ToolManager::cursorChangeRequested,
            this, [this](const QCursor &cursor) { setCursor(cursor); });

    // 蚂蚁线相位（对照 gimp_display_shell_selection 的 marching-ants-speed）
    // 间隔略放慢，且只局部 update，减轻缩小时整窗闪烁
    m_antsTimer->setInterval(100);
    connect(m_antsTimer, &QTimer::timeout, this, [this]() {
        m_antsPhase += 1.0;
        if (m_antsPhase >= 12.0)
            m_antsPhase = 0.0;
        // 只重绘图像区域，避免整控件闪
        if (m_document)
            update(imageRectInWidget().toAlignedRect().adjusted(-2, -2, 2, 2));
        else
            update();
    });

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
        // 选区单独订阅：只重画蚂蚁线，不重合成
        connect(m_document, &Ps::ImageDocument::selectionChanged, this, [this]() {
            rebuildSelectionOutlinePath();
            syncMarchingAntTimer();
            update();
        });
        rebuildCache();
        rebuildSelectionOutlinePath();
        syncMarchingAntTimer();
        if (width() > 50 && height() > 50)
            zoomFit();
        else
            m_pendingFit = true;
    } else {
        m_cache = QImage();
        m_antsPath = QPainterPath();
        m_pendingFit = false;
        syncMarchingAntTimer();
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
    m_toolContext.fillTolerance = m_fillTolerance;
    m_toolContext.fillContiguous = m_fillContiguous;
    m_toolContext.fillSource = m_fillSource;
    m_toolContext.fillOpacity = m_fillOpacity;
    m_toolContext.gradientType = m_gradientType;
    m_toolContext.gradientOpacity = m_gradientOpacity;
    m_toolContext.gradientOffsetPercent = m_gradientOffsetPercent;
    m_toolContext.gradientReverse = m_gradientReverse;
    m_toolContext.gradientDither = m_gradientDither;
    m_toolContext.viewZoom = m_zoom;
    m_toolContext.viewOffset = m_offset;

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

void CanvasView::setFillOptions(int tolerance, bool contiguous, int fillSource, qreal opacity)
{
    m_fillTolerance = qBound(0, tolerance, 255);
    m_fillContiguous = contiguous;
    m_fillSource = (fillSource == 1) ? 1 : 0; // 图案(2) 暂回退前景
    m_fillOpacity = qBound(0.0, opacity, 1.0);
    refreshToolContext();
}

void CanvasView::setGradientOptions(int type, qreal opacity, int offsetPercent,
                                    bool reverse, bool dither)
{
    m_gradientType = qBound(0, type, 4);
    m_gradientOpacity = qBound(0.0, opacity, 1.0);
    m_gradientOffsetPercent = qBound(0, offsetPercent, 100);
    m_gradientReverse = reverse;
    m_gradientDither = dither;
    refreshToolContext();
}

void CanvasView::updateToolCursor()
{
    if (m_toolManager)
        setCursor(m_toolManager->activeCursor());
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
    // 透明区棋盘格（格子 8、白/#c8c8c8）：与面板缩略图同一套实现，见 PixmapUtils
    PixmapUtils::paintChecker(painter, target.toAlignedRect(), 8,
                              QColor(255, 255, 255), QColor(200, 200, 200));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 4.0);
    painter.drawImage(target, m_cache);

    // 选区蚂蚁线（在图像之上、工具浮层之下）
    paintSelectionOutline(painter);

    // 工具浮层最后画：位于图像之上（对应 GIMP display 的 tool_items / preview_items）
    Ps::Tool *tool = m_toolManager ? m_toolManager->activeTool() : nullptr;
    if (tool && tool->hasOverlay()) {
        // 缩放/平移可能未走 refreshToolContext，绘制前同步视图变换
        m_toolContext.viewZoom = m_zoom;
        m_toolContext.viewOffset = m_offset;
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

    // 每次按下都同步上下文，避免 ToolManager::m_context 与画布侧脱节
    refreshToolContext();

    const Ps::ToolEvent e = makeToolEvent(event);

    // 通用平移手势（中键 / Alt+左键）优先于当前工具：由抓手工具承担。
    // 对齐 PS/GIMP：任何工具下都能临时平移。
    if (Ps::HandTool::isPanGesture(e)) {
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            // 上下文按值传给工具，工具不留副本（见 Tool::markDocumentDirty 的注释）
            m_panning = hand->mousePress(e, m_toolContext, *this);
            if (m_panning) {
                event->accept();
                return;
            }
        }
    }

    // 其余事件交给活动工具；未消费则视为无操作（不再有工具专属 if 分支）
    if (m_toolManager->dispatchPress(e, m_toolContext, *this)) {
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

    refreshToolContext();
    const Ps::ToolEvent e = makeToolEvent(event);

    // 平移进行中：固定发给抓手工具，不因中途切工具而丢失 release
    if (m_panning) {
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            hand->mouseMove(e, m_toolContext, *this);
            event->accept();
            return;
        }
    }

    if (m_toolManager->dispatchMove(e, m_toolContext, *this)) {
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

    refreshToolContext();
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

    if (m_toolManager->dispatchRelease(e, m_toolContext, *this)) {
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

void CanvasView::syncMarchingAntTimer()
{
    const bool need = m_document && !m_document->selection().isEmpty();
    if (need) {
        if (!m_antsTimer->isActive())
            m_antsTimer->start();
    } else if (m_antsTimer->isActive()) {
        m_antsTimer->stop();
        m_antsPhase = 0.0;
    }
}

void CanvasView::rebuildSelectionOutlinePath()
{
    // 【功能】从 mask 抽外轮廓。把共线的像素边合并成长线段，
    // 避免「每格一条 moveTo/lineTo」在缩小时又密又闪。
    m_antsPath = QPainterPath();
    if (!m_document)
        return;
    const Ps::Selection &sel = m_document->selection();
    if (sel.isEmpty())
        return;

    const QImage &mask = sel.mask();
    const QRect b = sel.bounds();
    auto isOn = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= mask.width() || y >= mask.height())
            return false;
        return mask.constScanLine(y)[x] > 0;
    };

    // 水平边：文档坐标 y = ey 上，合并连续的边界段
    for (int ey = b.top(); ey <= b.bottom() + 1; ++ey) {
        int runX0 = -1;
        for (int x = b.left(); x <= b.right() + 1; ++x) {
            const bool edge = (x <= b.right()) && (isOn(x, ey - 1) != isOn(x, ey));
            if (edge) {
                if (runX0 < 0)
                    runX0 = x;
            } else if (runX0 >= 0) {
                m_antsPath.moveTo(runX0, ey);
                m_antsPath.lineTo(x, ey);
                runX0 = -1;
            }
        }
    }

    // 竖直边：文档坐标 x = ex
    for (int ex = b.left(); ex <= b.right() + 1; ++ex) {
        int runY0 = -1;
        for (int y = b.top(); y <= b.bottom() + 1; ++y) {
            const bool edge = (y <= b.bottom()) && (isOn(ex - 1, y) != isOn(ex, y));
            if (edge) {
                if (runY0 < 0)
                    runY0 = y;
            } else if (runY0 >= 0) {
                m_antsPath.moveTo(ex, runY0);
                m_antsPath.lineTo(ex, y);
                runY0 = -1;
            }
        }
    }
}

void CanvasView::paintSelectionOutline(QPainter &painter)
{
    // 【功能】蚂蚁线：对照 gimp_display_shell_draw_selection_out（虚线描边）
    // 在**控件坐标**下描边：虚线按屏幕像素计，缩小时不会挤成一团。
    if (m_antsPath.isEmpty())
        return;

    QTransform toWidget;
    toWidget.translate(m_offset.x(), m_offset.y());
    toWidget.scale(m_zoom, m_zoom);
    const QPainterPath widgetPath = toWidget.map(m_antsPath);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);

    // 虚线长度用屏幕像素（cosmetic），黑白错相位形成 marching ants
    constexpr qreal kDash = 6.0;

    QPen white(Qt::white);
    white.setCosmetic(true);
    white.setWidth(1);
    white.setStyle(Qt::CustomDashLine);
    white.setDashPattern({kDash, kDash});
    white.setDashOffset(m_antsPhase);
    painter.setPen(white);
    painter.drawPath(widgetPath);

    QPen black(Qt::black);
    black.setCosmetic(true);
    black.setWidth(1);
    black.setStyle(Qt::CustomDashLine);
    black.setDashPattern({kDash, kDash});
    black.setDashOffset(m_antsPhase + kDash);
    painter.setPen(black);
    // 固定 1 屏幕像素错位（不要用 1/zoom，缩小时会抖）
    painter.translate(1.0, 1.0);
    painter.drawPath(widgetPath);
    painter.restore();
}

