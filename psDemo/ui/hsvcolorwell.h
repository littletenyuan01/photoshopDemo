#ifndef HSVCOLORWELL_H
#define HSVCOLORWELL_H

#include <QColor>
#include <QWidget>

/**
 * PS「颜色」面板主区：重叠的前景/背景色块 + 二维饱和度/明度色域 + 竖直色相条。
 *
 * 【对照】Adobe Photoshop 颜色面板（见用户提供的截图）：
 * 左 = 叠放方块（前景在前、背景错位在后）；中 = HSV 平面（S 横 / V 竖）；右 = 色相条。
 * 【对照 GIMP】`GimpColorArea` + 分量编辑器拆成多个控件；本项目按 PS 外观收成一块。
 *
 * 【阶段】只驱动本控件对外发出的 `colorChanged`；不写 document / AppSession。
 * 真接前景色时由 ColorsPanel / ToolBox 订阅即可。
 */
class HsvColorWell : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit HsvColorWell(QWidget *parent = nullptr);

    QColor color() const { return m_color; }
    /** 前景色；会刷新色域准星与色块。 */
    void setColor(const QColor &color);

    QColor backgroundColor() const { return m_bg; }
    void setBackgroundColor(const QColor &color);

signals:
    void colorChanged(const QColor &color);
    /** 点击交换前景/背景（UI 自洽；上层可接到工具箱）。 */
    void swatchesSwapped();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class DragTarget { None, Field, Hue };

    /** 收下 color 并同步 HSV 三分量（h<0 表示无色相，此时保留原色相）。 */
    void adoptColor(const QColor &color);
    void rebuildCaches();
    void applyFromFieldPos(QPoint pos);
    void applyFromHuePos(QPoint pos);
    QRect swatchFgRect() const;
    QRect swatchBgRect() const;
    QRect fieldRect() const;
    QRect hueRect() const;
    QPointF fieldCursor() const;

    QColor m_color = Qt::black;
    QColor m_bg = Qt::white;
    int m_hue = 0;           // 0–359
    int m_sat = 0;           // 0–255
    int m_val = 0;           // 0–255
    QImage m_fieldCache;     // 当前色相下的 S-V 平面
    QImage m_hueCache;       // 竖直色相条
    DragTarget m_drag = DragTarget::None;
};

#endif // HSVCOLORWELL_H
