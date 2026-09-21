#ifndef CANVASVIEW_H
#define CANVASVIEW_H

#include <QWidget>
#include <memory>

namespace Ps {
class ImageDocument;
}

/**
 * 画布视图（ui）。
 * 职责类似 GIMP 的 display：只负责显示与视图变换，不拥有图层像素。
 * 文档指针由 MainWindow 持有；本类监听 documentChanged 后重合成缓存。
 *
 * 坐标：
 * - 图像坐标：文档像素 (0,0)-(w,h)
 * - 控件坐标：widget 像素；图像左上角在控件中的位置为 m_offset，缩放为 m_zoom
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
    void setZoom(qreal zoom);
    void zoomFit();
    void zoomActual();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildCache();
    QPointF imageToWidget(const QPointF &imagePos) const;
    QRectF imageRectInWidget() const;
    /** 透明区域衬底，便于看出半透明像素。 */
    void drawCheckerboard(QPainter &painter, const QRect &rect) const;

    Ps::ImageDocument *m_document = nullptr;
    QImage m_cache;          // 最近一次合成结果（图像坐标）
    qreal m_zoom = 1.0;
    QPointF m_offset;        // 图像左上角在 widget 中的位置
    bool m_panning = false;
    QPoint m_lastMousePos;
};

#endif // CANVASVIEW_H
