#ifndef RULERWIDGET_H
#define RULERWIDGET_H

#include <QWidget>

/**
 * 标尺控件（ui，自绘）。
 *
 * 【对照 GIMP】
 * - GIMP：`GimpRuler` + `gimp_display_shell_rulers_update()`，用 lower/upper
 *   表示视口两端对应的图像坐标（可含单位换算）。
 * - 本项目瘦身：仅像素单位；无参考线拖出；无分辨率物理单位。
 *
 * 外层排布在 `canvasworkspace.ui`；本类只负责刻度绘制（允许无独立 .ui）。
 */
class RulerWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation)

public:
    explicit RulerWidget(QWidget *parent = nullptr);

    Qt::Orientation orientation() const { return m_orientation; }
    void setOrientation(Qt::Orientation orientation);

    /**
     * 设置标尺可见范围（图像像素坐标）。
     * 水平：lower=左缘对应图像 x，upper=右缘；垂直同理为 y。
     * 与 GIMP gimp_ruler_set_range(lower, upper, …) 同构（像素单位）。
     */
    void setRange(qreal lower, qreal upper);

    /** 鼠标在图像坐标中的位置；水平用 x，垂直用 y；NaN 表示不画指示线。 */
    void setCursorValue(qreal value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /** 选「好看」的主刻度步长（1/2/5×10^n）。 */
    static qreal niceStep(qreal rawStep);

    Qt::Orientation m_orientation = Qt::Horizontal;
    qreal m_lower = 0.0;
    qreal m_upper = 100.0;
    qreal m_cursor; // NaN = 不显示
};

#endif // RULERWIDGET_H
