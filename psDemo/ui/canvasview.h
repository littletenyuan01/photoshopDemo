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
 *
 * 平移约束（对齐常见 PS/GIMP 行为）：
 * - 图像小于视口：轴向上锁定居中，不能拖出窗口；
 * - 图像大于视口：可滚动，但边缘钳制在视口内，整幅图不会完全移出。
 */
class CanvasView : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasView(QWidget *parent = nullptr);

    void setDocument(Ps::ImageDocument *document);
    Ps::ImageDocument *document() const { return m_document; }

    qreal zoom() const { return m_zoom; }
    QPointF imageOffset() const { return m_offset; }

    /** 缩放后文档在视口中的像素尺寸。 */
    QSizeF contentSize() const;
    /** 当前视口尺寸。 */
    QSizeF viewportSize() const { return QSizeF(width(), height()); }

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

    void setCurrentTool(Ps::ToolId id);
    Ps::ToolId currentTool() const { return m_tool; }

    void setForegroundColor(const QColor &c);
    void setBackgroundColor(const QColor &c);
    void setBrushDiameter(int diameter);
    int brushDiameter() const { return qRound(m_brushRadius * 2.0); }

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
    bool m_pendingFit = false;

    Ps::ToolId m_tool = Ps::ToolId::Move;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
    qreal m_brushRadius = 10.0;

    bool m_painting = false;
    QPointF m_lastPaintPos;
};

#endif // CANVASVIEW_H
