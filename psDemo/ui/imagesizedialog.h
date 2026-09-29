#ifndef IMAGESIZEDIALOG_H
#define IMAGESIZEDIALOG_H

#include <QDialog>
#include <QImage>
#include <QSize>

QT_BEGIN_NAMESPACE
namespace Ui {
class ImageSizeDialog;
}
QT_END_NAMESPACE

namespace Ps {
class ImageDocument;
}

/**
 * Photoshop 风格「图像大小」对话框（.ui）。
 * 对齐 PS：左侧预览 + 右侧宽高/单位/分辨率/重新采样。
 * 确认后回传目标像素尺寸；是否重采样由 resampleEnabled()。
 */
class ImageSizeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageSizeDialog(Ps::ImageDocument *document, const QImage &preview,
                             QWidget *parent = nullptr);
    ~ImageSizeDialog() override;

    QSize resultPixelSize() const;
    bool resampleEnabled() const;

private:
    enum DimUnit { Pixels = 0, Inches = 1, Centimeters = 2, Millimeters = 3 };
    /** 分辨率单位（与 imagesizedialog.ui 中 resUnitCombo 顺序一致）。 */
    enum class ResUnit {
        PixelsPerInch = 0,
        PixelsPerCentimeter = 1,
    };

    void loadFromDocument();
    void resetToOriginal();
    void updateInfoLabels();
    void updatePreview();
    void onWidthEdited();
    void onHeightEdited();
    void onUnitChanged();
    void syncHeightUnitFromWidth();
    double resolutionPpi() const;
    QSizeF dimPixels() const;
    void setDimEditsFromPixels(double wPx, double hPx);
    static double toPixels(double value, DimUnit unit, double ppi);
    static double fromPixels(double px, DimUnit unit, double ppi);
    static QString formatDim(double value, DimUnit unit);

    Ui::ImageSizeDialog *ui;
    Ps::ImageDocument *m_document = nullptr;
    QImage m_previewSource;
    int m_origW = 0;
    int m_origH = 0;
    double m_aspect = 1.0;
    bool m_blockDim = false;
};

#endif // IMAGESIZEDIALOG_H
