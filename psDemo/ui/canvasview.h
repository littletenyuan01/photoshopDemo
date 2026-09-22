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
 * 像素写入不在本类内算算法，而是调用 engine/PaintEngine（对齐 GIMP tools↔paint 分离）。
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

    void setCurrentTool(Ps::ToolId id);
    Ps::ToolId currentTool() const { return m_tool; }

    void setForegroundColor(const QColor &c);
    void setBackgroundColor(const QColor &c);
    /** 画笔/橡皮直径（图像像素），内部存为半径。 */
    void setBrushDiameter(int diameter);
    int brushDiameter() const { return qRound(m_brushRadius * 2.0); }

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
    QPointF widgetToImage(const QPointF &widgetPos) const;
    QRectF imageRectInWidget() const;
    void drawCheckerboard(QPainter &painter, const QRect &rect) const;
    void updateToolCursor();

    /** 是否为「在活动层写像素」的工具。 */
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

    Ps::ToolId m_tool = Ps::ToolId::Move;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
    qreal m_brushRadius = 10.0; // 直径默认 20

    bool m_painting = false;
    QPointF m_lastPaintPos; // 上一颗 dab / 线段起点（图像坐标）
};

#endif // CANVASVIEW_H
