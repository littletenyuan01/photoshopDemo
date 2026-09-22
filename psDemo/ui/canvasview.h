#ifndef CANVASVIEW_H
#define CANVASVIEW_H

#include "tools/toolid.h"

#include <QColor>
#include <QWidget>

namespace Ps {
class ImageDocument;
}

/**
 * 画布视图（ui）。
 * 职责类似 GIMP display：显示与视图变换；并把指针事件交给当前工具逻辑。
 * 像素写入调用 engine/PaintEngine（对齐 GIMP tools↔paint 分离）。
 *
 * 坐标：
 * - 图像坐标：文档像素 (0,0)-(w,h)
 * - 控件坐标：widget 像素；图像左上角在控件中的位置为 m_offset，缩放为 m_zoom
 *
 * 默认：文档载入后 zoomFit，图像在视口内居中（对齐 PS）。
 */
class CanvasView : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);

    /** 不取得所有权；传 nullptr 清空显示。 */
    void setDocument(Ps::ImageDocument *document);
    Ps::ImageDocument *document() const { return m_document; }

    qreal zoom() const { return m_zoom; }
    QPointF imageOffset() const { return m_offset; }

    void setZoom(qreal zoom);
    /** 适应窗口并居中。 */
    void zoomFit();
    /** 100% 并居中。 */
    void zoomActual();
    /** 保持当前缩放，仅把图像中心对齐视口中心。 */
    void centerOnImage();

    void setCurrentTool(Ps::ToolId id);
    Ps::ToolId currentTool() const { return m_tool; }

    void setForegroundColor(const QColor &c);
    void setBackgroundColor(const QColor &c);
    void setBrushDiameter(int diameter);
    int brushDiameter() const { return qRound(m_brushRadius * 2.0); }

signals:
    /** 缩放或平移变化后发出，供标尺同步（对齐 GIMP rulers_update）。 */
    void viewChanged();
    /** 鼠标图像坐标；inside=false 表示离开画布。 */
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
    QPointF imageToWidget(const QPointF &imagePos) const;
    QPointF widgetToImage(const QPointF &widgetPos) const;
    QRectF imageRectInWidget() const;
    void drawCheckerboard(QPainter &painter, const QRect &rect) const;
    void updateToolCursor();

    bool isPaintTool() const;
    void beginPaintStroke(const QPointF &imagePos);
    void continuePaintStroke(const QPointF &imagePos);
    void endPaintStroke();

    Ps::ImageDocument *m_document = nullptr;
    QImage m_cache;
    qreal m_zoom = 1.0;
    QPointF m_offset;
    bool m_panning = false;
    QPoint m_lastMousePos;
    /** 首次有效尺寸时再 fit，避免构造时 width=0 导致无法居中。 */
    bool m_pendingFit = false;

    Ps::ToolId m_tool = Ps::ToolId::Move;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
    qreal m_brushRadius = 10.0;

    bool m_painting = false;
    QPointF m_lastPaintPos;
};

#endif // CANVASVIEW_H
