#ifndef CANVASVIEW_H
#define CANVASVIEW_H

#include "tools/toolcontext.h"
#include "tools/toolid.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

namespace Ps {
class ImageDocument;
class ToolManager;
struct ToolContext;
struct ToolEvent;
}

/**
 * 画布视图（ui）。
 *
 * 【职责】只做三件事，对应 GIMP display 层的边界：
 * 1. **视图变换**：缩放 / 平移 / 边缘钳制（对应 GimpDisplayShell 的 scale & scroll）
 * 2. **绘制**：合成缓存 + 棋盘格 + 工具浮层
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
     * scrollX/Y：内容左/上超出视口的量（≥0），与 QScrollBar::value 同构。
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

    // —— 工具 ——
    void setCurrentTool(Ps::ToolId id);
    Ps::ToolId currentTool() const;

    // —— 工具参数（会同步进 ToolContext）——
    void setForegroundColor(const QColor &c);
    void setBackgroundColor(const QColor &c);
    void setBrushDiameter(int diameter);
    int brushDiameter() const;

    // —— Ps::ViewPort 实现（供工具请求视图操作）——
    void zoomAt(const QPointF &widgetPos, qreal factor) override;
    void panBy(const QPointF &deltaWidget) override;
    void requestRepaint() override { update(); }

signals:
    void viewChanged();
    void cursorImagePosChanged(const QPointF &imagePos, bool inside);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void rebuildCache();
    void notifyViewChanged();
    /** 将 m_offset 钳制到合法范围，保证文档不整体移出视口。 */
    void clampOffset();
    QPointF imageToWidget(const QPointF &imagePos) const;
    QPointF widgetToImage(const QPointF &widgetPos) const;
    QRectF imageRectInWidget() const;

    /** 用当前成员状态重建 ToolContext（文档/颜色/笔刷任一变化后调用）。 */
    void refreshToolContext();
    /** 把 QMouseEvent 归一化成图像坐标的 ToolEvent。 */
    Ps::ToolEvent makeToolEvent(QMouseEvent *event) const;
    /** 按活动工具刷新鼠标光标。 */
    void updateToolCursor();

    Ps::ImageDocument *m_document = nullptr; ///< 不拥有；由 AppSession 持有
    QImage m_cache;
    qreal m_zoom = 1.0;
    QPointF m_offset;
    bool m_panning = false; ///< 通用平移手势（中键 / Alt+左键），由抓手工具承担
    bool m_pendingFit = false;

    Ps::ToolManager *m_toolManager = nullptr;
    Ps::ToolContext m_toolContext;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
    qreal m_brushRadius = 10.0;
};

#endif // CANVASVIEW_H
