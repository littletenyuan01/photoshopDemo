/**
 * canvasview.h — 画布视图声明（ui 层）。
 *
 * 视图变换、投影绘制、事件归一化转发；实现 ViewPort 供工具请求缩放/平移。
 */
#ifndef CANVASVIEW_H
#define CANVASVIEW_H

#include "tools/toolcontext.h"
#include "tools/toolid.h"
#include "engine/projection.h"

#include <QColor>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace Ps {
class ImageDocument;
class Layer;
class LayerFilterJob;
class ToolManager;
struct ToolContext;
struct ToolEvent;
}

/**
 * 画布视图（ui）。
 *
 * 【职责】只做三件事，对应 GIMP display 层的边界：
 * 1. **视图变换**：缩放 / 平移 / 边缘钳制（对应 GimpDisplayShell 的 scale & scroll）
 * 2. **绘制**：Projection 缓存 + 棋盘格 + 选区蚂蚁线 + 工具浮层
 * 3. **事件归一化并转发**：把 QMouseEvent 转成图像坐标的 ToolEvent 交给 ToolManager
 *
 * 【不再负责】具体工具逻辑。早先 `mousePressEvent` 里堆着「抓手/缩放/画笔/橡皮」
 * 的 if-else 链，每加一个工具都要改这个文件；现在工具逻辑全部在 `tools/` 层
 * （见 tools/tool.h 与 docs/architecture.md 主线 ①）。
 *
 * 【实现 ViewPort】工具通过本类暴露的 zoomAt / panBy 请求视图操作，
 * 从而让「缩放锚点数学」只存在于此一处，工具无需知道 zoom/offset 细节。
 *
 * 平移约束（对齐常见 PS/GIMP 行为）：
 * - 图像小于视口：轴向上锁定居中，不能拖出窗口；
 * - 图像大于视口：可滚动，但边缘钳制在视口内，整幅图不会完全移出。
 */
class CanvasView : public QWidget, public Ps::ViewPort
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);
    ~CanvasView() override;

    void setDocument(Ps::ImageDocument *document);
    Ps::ImageDocument *document() const { return m_document; }

    qreal zoom() const { return m_zoom; }
    QPointF imageOffset() const { return m_offset; }

    /** 缩放后文档在视口中的像素尺寸。 */
    QSizeF contentSize() const;

    /**
     * 由滚动条设置偏移（控件坐标）。
     * scrollX/Y 与 QScrollBar::value 同构：0 = 最「右/下推」极限，max = 最「左/上推」极限。
     * 含 pasteboard 过滚（对照 PS：文档缩小时滚动条仍在中间、可继续拖）。
     */
    void setScrollOffset(int scrollX, int scrollY);
    int scrollX() const;
    int scrollY() const;
    int scrollMaxX() const;
    int scrollMaxY() const;

    void setZoom(qreal zoom);
    void zoomFit();
    void zoomActual();
    void centerOnImage();

    /**
     * 当前视口对应的**文档可见矩形**（含外扩一块）。
     * 滤镜预览用它把重算范围限制在看得见的地方（见 ImageDocument::setLayerFilterNodePreview）。
     */
    QRect visibleDocumentRect() const;

    // —— 工具 ——
    void setCurrentTool(Ps::ToolId id);
    Ps::ToolId currentTool() const;

    /** 工具管理器（供 MainWindow 接线 TransformTool 等）。 */
    Ps::ToolManager *toolManager() const { return m_toolManager; }

    /** 刷新并返回当前工具上下文（选项栏提交/取消变换时用）。 */
    Ps::ToolContext toolContext();

    // —— 工具参数（会同步进 ToolContext）——
    void setForegroundColor(const QColor &c);
    void setBackgroundColor(const QColor &c);
    void setBrushDiameter(int diameter);
    int brushDiameter() const;
    /** 同步油漆桶选项（容差/连续/填充源/不透明度）。 */
    void setFillOptions(int tolerance, bool contiguous, Ps::FillSource fillSource, qreal opacity);
    /** 同步魔棒/快速选择选项（容差/连续/对所有图层取样）。 */
    void setSelectionFloodOptions(int tolerance, bool contiguous, bool sampleMerged);
    /** 同步磁性套索选项（宽度/对比度/频率）。 */
    void setMagneticLassoOptions(int width, int contrast, int frequency);
    /** 同步渐变选项（类型/不透明度/偏移/反向/仿色）。 */
    void setGradientOptions(Ps::GradientType type, qreal opacity, int offsetPercent,
                            bool reverse, bool dither);
    /** 同步仿制图章选项（对齐 / 对所有图层取样）。 */
    void setCloneStampOptions(bool align, bool sampleMerged);
    /** 同步形状选项（填充/描边/粗细/圆角/抗锯齿）。 */
    void setShapeOptions(bool fill, bool stroke, qreal strokeWidth,
                         qreal cornerRadius, bool antialias);
    /** 同步路径选项（多边形/自动添加/橡皮带）。 */
    void setPathOptions(bool polygonal, bool autoAdd, bool rubberBand);

    /**
     * 从当前投影缓存读取文档坐标像素（解预乘后的 RGB）。
     * 供信息面板实时取样；越界或投影未就绪返回无效色。
     */
    QColor sampleProjectionPixel(const QPointF &imagePos) const;

    // —— Ps::ViewPort 实现（供工具请求视图操作）——
    void zoomAt(const QPointF &widgetPos, qreal factor) override;
    void panBy(const QPointF &deltaWidget) override;
    void requestRepaint() override { update(); }

signals:
    void viewChanged();
    void cursorImagePosChanged(const QPointF &imagePos, bool inside);
    /** 吸管取前景色（转给 ToolBox / ColorsPanel）。 */
    void foregroundPicked(const QColor &color);
    /** 吸管取背景色。 */
    void backgroundPicked(const QColor &color);
    /**
     * 外部文件拖放到画布（对照 GIMP gimpdisplayshell-dnd：
     * 有文档→打开为图层；无文档→打开）。
     */
    void filesDropped(const QStringList &paths);

    /**
     * 滤镜定稿（重滤镜整层求值）的进度文案；空串 = 无后台任务。
     * 由底栏状态条显示（`CanvasDocStatusBar::setFilterProgressText`）。
     */
    void filterFinalizeProgress(const QString &text);

protected:
    bool event(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    /** 吸入文档脏区并视口优先合成；返回本轮重算的文档矩形。对照 gimp_projection_flush。 */
    QRect syncProjection();
    /** 把当前视口映成文档矩形，交给 Projection 优先合成。 */
    void updateProjectionPriorityRect();
    /** 空闲分块：继续合成挂起块并局部重画。 */
    void onProjectionIdle();
    void notifyViewChanged();

    /** 调整层拖参：定时器合并 + 视口 ROI 预览；松手整幅 adopt。 */
    void onAdjustmentPreview(int layerIndex);
    void onAdjustmentPreviewCommit(int layerIndex);
    void flushAdjustmentPreview();
    void clearAdjustmentLive();
    const QImage *activeLiveProjection() const;

    /**
     * 滤镜「定稿」的**异步链路**（见 engine/layerfilterjob.h）：
     *  `contentChanged` → 判断这次改动是否给活动层**新加了重滤镜** → 后台跑整层滤镜 →
     *  结果写入层的区域缓存 → 再 markDirty 让投影逐块合成（此时只做取子图 + 混合）。
     */
    void maybeStartAsyncFilterFinalize();
    void updateFilterFinalizeProgress();
    /** 取消并丢弃在跑的定稿任务（任何新改动/换文档时调用）。 */
    void cancelFilterFinalize();
    /** 活动层滤镜栈里最强算子的外扩（0 = 纯点滤镜，不值得异步）。 */
    static int strongestPadOfLayer(const Ps::Layer *layer);

    /**
     * **像素层滤镜预览**的异步化（与定稿共用 `LayerFilterJob`）。
     *
     * 由 `ImageDocument::layerFilterPreviewChanged` 直接触发（该信号**不**伴随
     * contentChanged —— 预览走静默标脏，见 `setLayerFilterNodePreview` 的约定）：
     * 重滤镜就起后台任务、本轮只重画旧投影；纯点滤镜仍退回同步（几毫秒）。
     */
    void onLayerFilterPreview(int layerIndex, const QRect &regionDoc);
    /** 起/接替一次后台滤镜任务；返回 false 表示参数不成立（调用方退回同步路径）。 */
    bool startFilterJobFor(int layerIndex, const QRect &want, int previewDivisor = 1);
    /** 将 m_offset 钳制到合法范围（含 pasteboard 过滚，对照 PS）。 */
    void clampOffset();
    /** 水平/竖直可平移的 offset 上下限（控件坐标）。 */
    void offsetRangeX(qreal *minOut, qreal *maxOut) const;
    void offsetRangeY(qreal *minOut, qreal *maxOut) const;
    QPointF imageToWidget(const QPointF &imagePos) const;
    QPointF widgetToImage(const QPointF &widgetPos) const;
    QRectF imageRectInWidget() const;

    /** 用当前成员状态重建 ToolContext（文档/颜色/笔刷任一变化后调用）。 */
    void refreshToolContext();
    /** 把 QMouseEvent 归一化成图像坐标的 ToolEvent。 */
    Ps::ToolEvent makeToolEvent(QMouseEvent *event) const;
    /** 按活动工具刷新鼠标光标。 */
    void updateToolCursor();
    /** 根据文档选区 mask 画蚂蚁线（对照 gimp_display_shell_draw_selection_*）。 */
    void paintSelectionOutline(QPainter &painter);
    /**
     * 高倍放大时画像素网格（对照 PS「像素网格」：View → Show → Pixel Grid）。
     * 仅在 zoom ≥ 500% 时绘制，避免低倍时密线糊成一片。
     */
    void paintPixelGrid(QPainter &painter, const QRectF &imageRectInWidget);
    /** 有选区时开蚂蚁线动画定时器，无选区时停。 */
    void syncMarchingAntTimer();
    /** 选区变化时从 mask 重建轮廓路径（文档坐标）。 */
    void rebuildSelectionOutlinePath();
    /** Caps Lock 开时画工具作用范围圈（对照 PS 笔尖指示；半径来自 Tool::outlineRadius）。 */
    void paintToolOutline(QPainter &painter);
    /**
     * 结束空格临时抓手（松键且未在拖、或拖完松键后调用）。
     * 恢复 beginTemporaryTool 压栈的原工具。
     */
    void endSpaceHandIfIdle();

    Ps::ImageDocument *m_document = nullptr; ///< 不拥有；由 AppSession 持有
    Ps::Projection m_projection;             ///< 只读投影缓存（对照 GimpProjection）
    qreal m_zoom = 1.0;
    QPointF m_offset;
    bool m_panning = false; ///< 通用平移手势（中键 / Alt+左键 / 空格+拖），由抓手工具承担
    bool m_pendingFit = false;
    bool m_spaceHeld = false; ///< 按住空格：临时抓手（对齐 PS）

    Ps::ToolManager *m_toolManager = nullptr;
    Ps::ToolContext m_toolContext;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
    qreal m_brushRadius = 10.0;
    int m_fillTolerance = 32;
    bool m_fillContiguous = true;
    Ps::FillSource m_fillSource = Ps::FillSource::Foreground;
    qreal m_fillOpacity = 1.0;

    int m_selTolerance = 32;
    bool m_selContiguous = true;
    bool m_selSampleMerged = false;

    int m_magneticWidth = 10;
    int m_magneticContrast = 40;
    int m_magneticFrequency = 57;

    Ps::GradientType m_gradientType = Ps::GradientType::Linear;
    qreal m_gradientOpacity = 1.0;
    int m_gradientOffsetPercent = 0;
    bool m_gradientReverse = false;
    bool m_gradientDither = true;

    bool m_cloneAlign = true;
    bool m_cloneSampleMerged = false;

    bool m_shapeFill = true;
    bool m_shapeStroke = false;
    qreal m_shapeStrokeWidth = 2.0;
    qreal m_shapeCornerRadius = 0.0;
    bool m_shapeAntialias = true;

    bool m_pathPolygonal = false;
    bool m_pathAutoAdd = true;
    bool m_pathRubberBand = true;

    QTimer *m_antsTimer = nullptr; ///< 蚂蚁线虚线相位动画
    QTimer *m_projIdleTimer = nullptr; ///< 投影空闲分块（对照 GIMP projection idle）
    QTimer *m_adjPreviewTimer = nullptr; ///< 合并滑条 tick，先让滑条重绘再算预览
    qreal m_antsPhase = 0.0;
    QPainterPath m_antsPath; ///< 选区轮廓（文档像素坐标）；selectionChanged 时重建

    QPointF m_pointerImagePos; ///< 最近指针文档坐标（Caps Lock 笔尖圈用）
    bool m_pointerInside = false;

    // —— 调整层预览（对照 GIMP/GEGL：输入缓存一次，参数变只后台重跑滤镜）——
    int m_adjLiveLayer = -1;
    int m_adjPendingLayer = -1;
    int m_adjPreviewGen = 0;     ///< 丢弃过期后台结果
    bool m_adjPreviewBusy = false; ///< 单飞：同时只跑一个后台任务
    bool m_adjPreviewDirty = false; ///< busy 期间又有新参数 → 结束后再跑
    QRect m_adjStackRect; ///< below/above 缓存对应的文档矩形（拖参期间固定）
    QImage m_adjBelow;  ///< 下方合成缓存（拖中为 ROI 尺寸；GEGL input）
    QImage m_adjAbove;  ///< 调整层之上（同上）
    QImage m_adjLive;   ///< 画布预览（文档全尺寸）

    // —— 滤镜定稿：异步任务（engine/layerfilterjob.h）——
    Ps::LayerFilterJob *m_filterJob = nullptr; ///< 惰性创建，归本控件
    QTimer *m_filterProgressTimer = nullptr;   ///< 轮询进度 → 底栏文案
    int m_filterJobLayer = -1;                 ///< 在跑的定稿任务目标层；-1 = 无
    /** 最近一次「看见的层滤镜栈状态」指纹：变了才值得再起一次异步定稿。 */
    quint32 m_filterJobFilterPrint = 0;
    bool m_filterJobHavePrint = false;
};

#endif // CANVASVIEW_H
