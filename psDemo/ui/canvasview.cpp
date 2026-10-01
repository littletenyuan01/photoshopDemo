/**
 * canvasview.cpp — 画布视图实现（ui 层）。
 *
 * 不含工具分支；ToolManager 分发、蚂蚁线、像素网格、通用平移手势。
 */
#include "canvasview.h"

#include "domain/imagedocument.h"
#include "domain/selection.h"
#include "engine/premul.h"
#include "pixmaputils.h"
#include "tools/handtool.h"
#include "tools/tool.h"
#include "tools/toolcontext.h"
#include "tools/toolevent.h"
#include "tools/toolmanager.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QShowEvent>
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
    connect(m_toolManager, &Ps::ToolManager::foregroundPicked,
            this, &CanvasView::foregroundPicked);
    connect(m_toolManager, &Ps::ToolManager::backgroundPicked,
            this, &CanvasView::backgroundPicked);

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
        // 图层属性变化（显隐/透明度）也走 contentChanged，画布需重投影，
        // 合成细节在 Projection / Compositor，画布不必区分。
        connect(m_document, &Ps::ImageDocument::contentChanged, this, [this]() {
            syncProjection();
            update();
        });
        // 选区单独订阅：只重画蚂蚁线，不重合成
        connect(m_document, &Ps::ImageDocument::selectionChanged, this, [this]() {
            rebuildSelectionOutlinePath();
            syncMarchingAntTimer();
            update();
        });
        m_projection.bind(m_document);
        syncProjection();
        rebuildSelectionOutlinePath();
        syncMarchingAntTimer();
        // 启动默认在主页时工作区是隐藏的：此时 width/height 往往是未布局完的小值，
        // 若立刻 zoomFit 会得到错误的十几 %（窗口拉开后也不会再 fit）。等真正显示再适配。
        if (isVisible() && width() > 50 && height() > 50)
            zoomFit();
        else
            m_pendingFit = true;
    } else {
        m_projection.bind(nullptr);
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

/**
 * pasteboard 过滚边距（对照 PS）：约为半个视口。
 * 文档小于视口时仍能左右/上下拖；居中时滚动条落在行程中点。
 */
static qreal pasteboardMargin(qreal viewportSide)
{
    return qMax(64.0, viewportSide * 0.5);
}

void CanvasView::offsetRangeX(qreal *minOut, qreal *maxOut) const
{
    const QSizeF content = contentSize();
    const qreal vw = width();
    const qreal margin = pasteboardMargin(vw);
    // max：图像左缘可到的最右位置；min：可到的最左位置
    *maxOut = margin;
    *minOut = vw - content.width() - margin;
}

void CanvasView::offsetRangeY(qreal *minOut, qreal *maxOut) const
{
    const QSizeF content = contentSize();
    const qreal vh = height();
    const qreal margin = pasteboardMargin(vh);
    *maxOut = margin;
    *minOut = vh - content.height() - margin;
}

void CanvasView::clampOffset()
{
    if (!m_document) {
        m_offset = QPointF();
        return;
    }

    qreal minX = 0;
    qreal maxX = 0;
    qreal minY = 0;
    qreal maxY = 0;
    offsetRangeX(&minX, &maxX);
    offsetRangeY(&minY, &maxY);
    m_offset.setX(qBound(minX, m_offset.x(), maxX));
    m_offset.setY(qBound(minY, m_offset.y(), maxY));
}

int CanvasView::scrollMaxX() const
{
    if (!m_document)
        return 0;
    qreal minX = 0;
    qreal maxX = 0;
    offsetRangeX(&minX, &maxX);
    return qMax(0, qCeil(maxX - minX));
}

int CanvasView::scrollMaxY() const
{
    if (!m_document)
        return 0;
    qreal minY = 0;
    qreal maxY = 0;
    offsetRangeY(&minY, &maxY);
    return qMax(0, qCeil(maxY - minY));
}

int CanvasView::scrollX() const
{
    const int mx = scrollMaxX();
    if (mx <= 0)
        return 0;
    qreal minX = 0;
    qreal maxX = 0;
    offsetRangeX(&minX, &maxX);
    // scroll 0 ↔ offset=maxX；scroll max ↔ offset=minX
    return qBound(0, qRound(maxX - m_offset.x()), mx);
}

int CanvasView::scrollY() const
{
    const int my = scrollMaxY();
    if (my <= 0)
        return 0;
    qreal minY = 0;
    qreal maxY = 0;
    offsetRangeY(&minY, &maxY);
    return qBound(0, qRound(maxY - m_offset.y()), my);
}

void CanvasView::setScrollOffset(int scrollX, int scrollY)
{
    if (!m_document)
        return;

    qreal minX = 0;
    qreal maxX = 0;
    qreal minY = 0;
    qreal maxY = 0;
    offsetRangeX(&minX, &maxX);
    offsetRangeY(&minY, &maxY);

    const int mx = qMax(0, qCeil(maxX - minX));
    const int my = qMax(0, qCeil(maxY - minY));
    m_offset.setX(maxX - qreal(qBound(0, scrollX, mx)));
    m_offset.setY(maxY - qreal(qBound(0, scrollY, my)));

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
    m_toolContext.selTolerance = m_selTolerance;
    m_toolContext.selContiguous = m_selContiguous;
    m_toolContext.selSampleMerged = m_selSampleMerged;
    m_toolContext.magneticWidth = m_magneticWidth;
    m_toolContext.magneticContrast = m_magneticContrast;
    m_toolContext.magneticFrequency = m_magneticFrequency;
    m_toolContext.gradientType = m_gradientType;
    m_toolContext.gradientOpacity = m_gradientOpacity;
    m_toolContext.gradientOffsetPercent = m_gradientOffsetPercent;
    m_toolContext.gradientReverse = m_gradientReverse;
    m_toolContext.gradientDither = m_gradientDither;
    m_toolContext.cloneAlign = m_cloneAlign;
    m_toolContext.cloneSampleMerged = m_cloneSampleMerged;
    m_toolContext.shapeFill = m_shapeFill;
    m_toolContext.shapeStroke = m_shapeStroke;
    m_toolContext.shapeStrokeWidth = m_shapeStrokeWidth;
    m_toolContext.shapeCornerRadius = m_shapeCornerRadius;
    m_toolContext.shapeAntialias = m_shapeAntialias;
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

void CanvasView::setFillOptions(int tolerance, bool contiguous, Ps::FillSource fillSource, qreal opacity)
{
    m_fillTolerance = qBound(0, tolerance, 255);
    m_fillContiguous = contiguous;
    // 图案填充尚未实现：回退前景，避免静默用错颜色
    m_fillSource = (fillSource == Ps::FillSource::Background)
                       ? Ps::FillSource::Background
                       : Ps::FillSource::Foreground;
    m_fillOpacity = qBound(0.0, opacity, 1.0);
    refreshToolContext();
}

void CanvasView::setSelectionFloodOptions(int tolerance, bool contiguous, bool sampleMerged)
{
    m_selTolerance = qBound(0, tolerance, 255);
    m_selContiguous = contiguous;
    m_selSampleMerged = sampleMerged;
    refreshToolContext();
}

void CanvasView::setMagneticLassoOptions(int width, int contrast, int frequency)
{
    m_magneticWidth = qBound(1, width, 256);
    m_magneticContrast = qBound(1, contrast, 100);
    m_magneticFrequency = qBound(1, frequency, 100);
    refreshToolContext();
    update(); // Caps Lock 搜索圈半径随宽度即时刷新
}

void CanvasView::setGradientOptions(Ps::GradientType type, qreal opacity, int offsetPercent,
                                    bool reverse, bool dither)
{
    m_gradientType = type;
    m_gradientOpacity = qBound(0.0, opacity, 1.0);
    m_gradientOffsetPercent = qBound(0, offsetPercent, 100);
    m_gradientReverse = reverse;
    m_gradientDither = dither;
    refreshToolContext();
}

void CanvasView::setCloneStampOptions(bool align, bool sampleMerged)
{
    m_cloneAlign = align;
    m_cloneSampleMerged = sampleMerged;
    refreshToolContext();
}

void CanvasView::setShapeOptions(bool fill, bool stroke, qreal strokeWidth,
                                 qreal cornerRadius, bool antialias)
{
    m_shapeFill = fill;
    m_shapeStroke = stroke;
    m_shapeStrokeWidth = qMax(1.0, strokeWidth);
    m_shapeCornerRadius = qMax(0.0, cornerRadius);
    m_shapeAntialias = antialias;
    refreshToolContext();
}

QColor CanvasView::sampleProjectionPixel(const QPointF &imagePos) const
{
    if (m_projection.isNull())
        return QColor();
    const QImage &img = m_projection.image();
    const int x = qFloor(imagePos.x());
    const int y = qFloor(imagePos.y());
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
        return QColor();

    const QRgb px = reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
    int r = 0, g = 0, b = 0, a = 0;
    Ps::Premul::unpremultiplyRgb(px, &r, &g, &b, &a);
    if (a <= 0)
        return QColor(0, 0, 0, 0);
    return QColor(r, g, b, a);
}

void CanvasView::updateToolCursor()
{
    if (m_spaceHeld && !m_panning) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (m_toolManager)
        setCursor(m_toolManager->activeCursor());
}

void CanvasView::endSpaceHandIfIdle()
{
    // 仍按着空格或平移拖拽未结束：保持临时抓手
    if (m_spaceHeld || m_panning || !m_toolManager)
        return;
    if (m_toolManager->hasTemporaryTool())
        m_toolManager->endTemporaryTool();
    updateToolCursor();
    update(); // 恢复原工具浮层（套索橡皮筋等）
}

// —— 绘制 ——

void CanvasView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(45, 45, 48));

    if (!m_document || m_projection.isNull()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, tr("无文档 — 请新建或打开图像"));
        return;
    }

    const QRectF target = imageRectInWidget();
    // 透明区棋盘格（格子 8、白/#c8c8c8）：与面板缩略图同一套实现，见 PixmapUtils
    PixmapUtils::paintChecker(painter, target.toAlignedRect(), 8,
                              QColor(255, 255, 255), QColor(200, 200, 200));
    // 缩小才平滑插值；放大用最近邻，才能看出「一个个像素块」（对齐 PS）
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    painter.drawImage(target, m_projection.image());

    // 像素网格（PS 约 ≥500% 出现）
    paintPixelGrid(painter, target);

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

    // Caps Lock：笔刷类工具的作用范围圈（磁性套索自绘搜索圈，outlineRadius=0）
    paintToolOutline(painter);
}

// —— 事件 ——

void CanvasView::wheelEvent(QWheelEvent *event)
{
    if (!m_document) {
        QWidget::wheelEvent(event);
        return;
    }

    // 对照 PS：Alt+滚轮缩放；Ctrl+滚轮左右平移；裸滚轮上下平移（空格临时抓手见 keyPress）。
    const Qt::KeyboardModifiers mods = event->modifiers();
    const QPoint angle = event->angleDelta();
    const QPoint pixels = event->pixelDelta();

    if (mods.testFlag(Qt::AltModifier)) {
        // 触控板常把纵向滚到 x；优先 y，否则用 x
        const int wheel = angle.y() != 0 ? angle.y() : angle.x();
        if (wheel == 0) {
            event->ignore();
            return;
        }
        const qreal factor = wheel > 0 ? 1.1 : (1.0 / 1.1);
        zoomAt(event->position(), factor);
        event->accept();
        return;
    }

    // 平移步长：优先 pixelDelta（触控板），否则按 120°/格 折成约 48px
    QPointF step;
    if (!pixels.isNull()) {
        step = QPointF(pixels);
    } else {
        constexpr qreal kPxPerNotch = 48.0;
        step = QPointF(angle.x() * kPxPerNotch / 120.0,
                       angle.y() * kPxPerNotch / 120.0);
    }

    if (mods.testFlag(Qt::ControlModifier)) {
        // Ctrl+纵向滚轮 → 水平平移；触控板横向手势仍走 x
        const qreal dx = step.y() != 0.0 ? step.y() : step.x();
        if (dx != 0.0)
            panBy(QPointF(dx, 0.0));
    } else {
        // 裸滚轮：纵向平移；仅有水平分量时顺带左右滑（触控板）
        if (step.y() != 0.0)
            panBy(QPointF(0.0, step.y()));
        else if (step.x() != 0.0)
            panBy(QPointF(step.x(), 0.0));
    }
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
    e.doubleClick = (event->type() == QEvent::MouseButtonDblClick);
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

    // 空格已 beginTemporaryTool(Hand) 时，活动工具就是抓手，走下方 dispatch 即可。
    // 中键：未切抓手时的临时平移。Alt+左键不平移（图章设源 / 其它 Alt 手势）。
    const bool onHand = (currentTool() == Ps::ToolId::Hand);
    if (!onHand && Ps::HandTool::isPanGesture(e)) {
        if (Ps::Tool *hand = m_toolManager->tool(Ps::ToolId::Hand)) {
            m_panning = hand->mousePress(e, m_toolContext, *this);
            if (m_panning) {
                event->accept();
                return;
            }
        }
    }

    // 其余事件交给活动工具（含空格临时抓手）
    if (m_toolManager->dispatchPress(e, m_toolContext, *this)) {
        // 抓手消费左键/中键按下 → 标记平移中，便于松空格时延后恢复原工具
        if (onHand && (e.isLeft() || e.isMiddle()))
            m_panning = true;
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Qt 默认不把 DblClick 转成 mousePress；多边形/磁性套索靠 doubleClick 闭合
    mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_document) {
        m_pointerImagePos = widgetToImage(event->position());
        m_pointerInside = true;
        emit cursorImagePosChanged(m_pointerImagePos, true);
        // Caps Lock 笔尖圈需随指针重画
        if (Ps::Tool::capsLockOn())
            update();
    }

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
            endSpaceHandIfIdle();
            event->accept();
            return;
        }
    }

    if (m_toolManager->dispatchRelease(e, m_toolContext, *this)) {
        endSpaceHandIfIdle();
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

void CanvasView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_pendingFit && m_document && width() > 50 && height() > 50)
        zoomFit();
}

void CanvasView::leaveEvent(QEvent *event)
{
    m_pointerInside = false;
    emit cursorImagePosChanged(QPointF(), false);
    if (Ps::Tool::capsLockOn())
        update();
    QWidget::leaveEvent(event);
}

bool CanvasView::event(QEvent *event)
{
    // 工具编辑中抢走菜单快捷键（如 Delete=清除），再交给 keyPressEvent 分发
    if (event->type() == QEvent::ShortcutOverride && m_toolManager && m_document) {
        const auto *ke = static_cast<const QKeyEvent *>(event);
        if (m_toolManager->wantsShortcutOverride(ke->key(), ke->modifiers())) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void CanvasView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = true;
        // 其他工具下：临时切到抓手（不 deactivate 原工具，松键后恢复）
        if (m_toolManager && m_document) {
            refreshToolContext();
            if (m_toolManager->beginTemporaryTool(Ps::ToolId::Hand))
                update();
        }
        if (!m_panning)
            setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    // 活动工具优先消费（多边形套索 Enter/Esc/Backspace 等）
    if (m_document && m_toolManager && !event->isAutoRepeat()) {
        refreshToolContext();
        if (m_toolManager->dispatchKeyPress(event->key(), event->modifiers(),
                                            m_toolContext, *this)) {
            event->accept();
            return;
        }
    }

    // 画笔等工具不消费 Caps Lock：画布自行刷新笔尖范围圈
    if (event->key() == Qt::Key_CapsLock && !event->isAutoRepeat()) {
        update();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceHeld = false;
        endSpaceHandIfIdle();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

// —— 内部工具 ——

void CanvasView::syncProjection()
{
    m_projection.sync();
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

void CanvasView::paintPixelGrid(QPainter &painter, const QRectF &imageRectInWidget)
{
    // 【功能】对照 PS View → Show → Pixel Grid：高倍下在像素边界画细线
    // 阈值：≥ 500%（与 PS 默认「足够大才显示」一致）；再低会密成灰雾
    constexpr qreal kMinZoomForGrid = 5.0;
    if (!m_document || m_zoom < kMinZoomForGrid)
        return;

    const QRectF clip = imageRectInWidget.intersected(QRectF(rect()));
    if (clip.isEmpty())
        return;

    // 可见文档像素范围（向外扩 1，盖住边缘）
    const QPointF tl = widgetToImage(clip.topLeft());
    const QPointF br = widgetToImage(clip.bottomRight());
    const int x0 = qMax(0, qFloor(tl.x()));
    const int y0 = qMax(0, qFloor(tl.y()));
    const int x1 = qMin(m_document->width(), qCeil(br.x()));
    const int y1 = qMin(m_document->height(), qCeil(br.y()));
    if (x1 <= x0 || y1 <= y0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setClipRect(clip);

    QPen pen(QColor(0, 0, 0, 28));
    pen.setWidth(0); // cosmetic hairline
    pen.setCosmetic(true);
    painter.setPen(pen);

    const qreal top = m_offset.y();
    const qreal bottom = m_offset.y() + m_document->height() * m_zoom;
    const qreal left = m_offset.x();
    const qreal right = m_offset.x() + m_document->width() * m_zoom;

    for (int x = x0; x <= x1; ++x) {
        const qreal wx = m_offset.x() + x * m_zoom;
        painter.drawLine(QPointF(wx, top), QPointF(wx, bottom));
    }
    for (int y = y0; y <= y1; ++y) {
        const qreal wy = m_offset.y() + y * m_zoom;
        painter.drawLine(QPointF(left, wy), QPointF(right, wy));
    }

    painter.restore();
}

void CanvasView::paintToolOutline(QPainter &painter)
{
    if (!m_pointerInside || !m_toolManager || !Ps::Tool::capsLockOn())
        return;

    Ps::Tool *tool = m_toolManager->activeTool();
    if (!tool)
        return;

    refreshToolContext();
    const qreal radiusDoc = tool->outlineRadius(m_toolContext);
    if (radiusDoc <= 0.0 || m_zoom <= 0.0)
        return;

    const QPointF c = imageToWidget(m_pointerImagePos);
    const qreal r = radiusDoc * m_zoom;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(255, 255, 255, 200));
    pen.setCosmetic(true);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(c, r, r);
    pen.setColor(QColor(0, 0, 0, 160));
    painter.setPen(pen);
    painter.drawEllipse(c, r + 1.0, r + 1.0);
    painter.restore();
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

