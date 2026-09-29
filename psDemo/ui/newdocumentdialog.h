/**
 * newdocumentdialog.h —「新建文档」对话框声明（ui 层）。
 *
 * 预设卡片 + 详情栏；创建回传像素尺寸与文档名，颜色模式等尚未进 domain。
 */
#ifndef NEWDOCUMENTDIALOG_H
#define NEWDOCUMENTDIALOG_H

#include <QDialog>
#include <QSize>

class QEvent;

QT_BEGIN_NAMESPACE
namespace Ui {
class NewDocumentDialog;
}
QT_END_NAMESPACE

/**
 * Photoshop 风格「新建文档」对话框壳。
 *
 * 【对齐 PS】分类 Tab + 左侧预设卡片 + 右侧「预设详细信息」
 * （宽高 / 方向 / 分辨率 / 颜色模式 / 背景 / 高级选项）+ 创建/关闭。
 *
 * 【对照 GIMP】`app/dialogs/image-new-dialog.c`（Create a New Image）+
 * `app/widgets/gimptemplateeditor.c`（模板尺寸编辑器）。
 * GIMP 是模板下拉 + TemplateEditor；本项目按 PS 截图做成预设网格 + 详情栏。
 *
 * ⚠️ UI 阶段：创建仅回传宽高与文档名；颜色模式/分辨率等尚未进 domain。
 */
class NewDocumentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewDocumentDialog(QWidget *parent = nullptr);
    ~NewDocumentDialog() override;

    /** 创建按钮确认后的像素尺寸（由当前单位 + 分辨率换算）。 */
    QSize documentSize() const;
    QString documentName() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum DimUnit { Pixels = 0, Inches = 1, Centimeters = 2, Millimeters = 3 };
    /** 分辨率单位（与 newdocumentdialog.ui 中 resUnitCombo 顺序一致）。 */
    enum class ResUnit {
        PixelsPerInch = 0,
        PixelsPerCentimeter = 1,
    };

    void applyPreset(int widthPx, int heightPx, int ppi, DimUnit unit, const QString &title);
    void updateOrientationButtons();
    void onDimUnitChanged(int newIndex);
    void onResolutionEdited();
    void syncDimValidators();
    void setDimEditsFromPixels(double widthPx, double heightPx);
    QSizeF dimPixels() const;
    double resolutionPpi() const;
    static double toPixels(double value, DimUnit unit, double ppi);
    static double fromPixels(double px, DimUnit unit, double ppi);
    static QString formatDim(double value, DimUnit unit);

    Ui::NewDocumentDialog *ui;
    DimUnit m_dimUnit = Pixels; ///< 当前宽高编辑单位（与 dimUnitCombo 同步）
};

#endif // NEWDOCUMENTDIALOG_H
