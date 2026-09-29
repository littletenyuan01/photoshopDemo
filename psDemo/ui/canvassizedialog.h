/**
 * canvassizedialog.h —「画布大小」对话框声明（ui 层）。
 *
 * 当前/新建大小、相对、定位锚点、扩展颜色。
 */
#ifndef CANVASSIZEDIALOG_H
#define CANVASSIZEDIALOG_H

#include <QColor>
#include <QDialog>
#include <QSize>

class QButtonGroup;

QT_BEGIN_NAMESPACE
namespace Ui {
class CanvasSizeDialog;
}
QT_END_NAMESPACE

namespace Ps {
class ImageDocument;
}

/**
 * Photoshop 风格「画布大小」对话框（.ui）。
 * 对齐 PS：当前大小 / 新建大小 / 相对 / 定位锚点 / 扩展颜色。
 */
class CanvasSizeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CanvasSizeDialog(Ps::ImageDocument *document,
                              const QColor &foreground,
                              const QColor &background,
                              QWidget *parent = nullptr);
    ~CanvasSizeDialog() override;

    /** 确认后的绝对画布像素尺寸。 */
    QSize resultPixelSize() const;
    /** 锚点 0..2 行、0..2 列（0=上/左，1=中，2=下/右）。 */
    int anchorRow() const;
    int anchorCol() const;
    QColor extensionColor() const;

private:
    enum DimUnit { Pixels = 0, Inches = 1, Centimeters = 2, Millimeters = 3 };
    /** 扩展颜色下拉（与 canvassizedialog.ui 项顺序一致）。 */
    enum class ExtensionColor {
        Background = 0,
        Foreground,
        White,
        Black,
        Transparent,
    };

    void loadFromDocument();
    void resetToOriginal();
    void updateLabels();
    void onExtColorChanged();
    void syncHeightUnitFromWidth();
    QSizeF absolutePixelSize() const;
    static double toPixels(double value, DimUnit unit, double ppi);

    Ui::CanvasSizeDialog *ui;
    Ps::ImageDocument *m_document = nullptr; ///< 不拥有；用于读当前画布尺寸
    QColor m_fg;                             ///< 扩展色「前景」选项用
    QColor m_bg;                             ///< 扩展色「背景」选项用
    int m_origW = 0;                         ///< 打开对话框时的原始宽（像素）
    int m_origH = 0;                         ///< 打开对话框时的原始高（像素）
    QButtonGroup *m_anchorGroup = nullptr;   ///< 3×3 定位锚点互斥组
    double m_ppi = 72.0;                   ///< 显示用默认分辨率（尚无 domain 字段）
};

#endif // CANVASSIZEDIALOG_H
