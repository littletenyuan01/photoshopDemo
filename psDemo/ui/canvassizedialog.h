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

    void loadFromDocument();
    void resetToOriginal();
    void updateLabels();
    void onExtColorChanged();
    void syncHeightUnitFromWidth();
    QSizeF absolutePixelSize() const;
    static double toPixels(double value, DimUnit unit, double ppi);

    Ui::CanvasSizeDialog *ui;
    Ps::ImageDocument *m_document = nullptr;
    QColor m_fg;
    QColor m_bg;
    int m_origW = 0;
    int m_origH = 0;
    QButtonGroup *m_anchorGroup = nullptr;
    double m_ppi = 72.0;
};

#endif // CANVASSIZEDIALOG_H
