#ifndef COLORPICKERDIALOG_H
#define COLORPICKERDIALOG_H

#include <QColor>
#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class ColorPickerDialog;
}
QT_END_NAMESPACE

/**
 * Photoshop 风格拾色器（前景色/背景色）。
 *
 * 布局见 ui/colorpickerdialog.ui：
 * - 左侧：二维色板 + 竖条色相滑杆 +「只有 Web 颜色」
 * - 中部：新的/当前预览 + HSB / RGB / Lab / CMYK / #Hex
 * - 右侧：确定 / 复位 / 添加到色板 / 颜色库
 *
 * 单选按钮决定主色板与滑杆分别对应哪个分量（与 PS 相同）。
 * 色板 / 色条 / 预览为自定义绘制控件，在 C++ 中嵌入 .ui 占位容器。
 */
class ColorPickerDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Foreground, Background };

    explicit ColorPickerDialog(const QColor &initial,
                               Mode mode = Mode::Foreground,
                               QWidget *parent = nullptr);
    ~ColorPickerDialog() override;

    QColor selectedColor() const { return m_color; }

    /** 模态弹出；取消返回无效色。 */
    static QColor getColor(const QColor &initial,
                           QWidget *parent,
                           Mode mode = Mode::Foreground);

private:
    enum class Channel {
        H, S, V,
        R, G, B,
        L, A, LabB
    };

    void wireUi(Mode mode);
    void setColorInternal(const QColor &c, bool updateFields);
    void syncFieldsFromColor();
    void applyWebSafeIfNeeded();
    QColor colorFromChannelMap(qreal x, qreal y, qreal z) const;
    void channelMapToXyz(qreal *x, qreal *y, qreal *z) const;
    void rebuildGradients();

    void onFieldChanged(qreal nx, qreal ny);
    void onSliderChanged(qreal nz);
    void onChannelToggled();
    void onHsVEdited();
    void onRgbEdited();
    void onLabEdited();
    void onCmykEdited();
    void onHexEdited();
    void onReset();
    void onAddToSwatches();
    void onColorLibraries();

    static void rgbToLab(const QColor &c, qreal *L, qreal *a, qreal *b);
    static QColor labToRgb(qreal L, qreal a, qreal b);
    static void rgbToCmyk(const QColor &c, int *C, int *M, int *Y, int *K);
    static QColor cmykToRgb(int C, int M, int Y, int K);
    static QColor nearestWebSafe(const QColor &c);

    Ui::ColorPickerDialog *ui = nullptr;

    QColor m_color {Qt::white};
    QColor m_original {Qt::white};
    Channel m_channel = Channel::H;
    bool m_updating = false;

    class ColorPlaneWidget;
    class ColorStripWidget;
    class ColorPreviewWidget;

    ColorPlaneWidget *m_plane = nullptr;
    ColorStripWidget *m_strip = nullptr;
    ColorPreviewWidget *m_preview = nullptr;
};

#endif // COLORPICKERDIALOG_H
