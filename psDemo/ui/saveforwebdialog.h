/**
 * saveforwebdialog.h —「存储为 Web 所用格式」对话框（ui 层）。
 *
 * 2-up 原稿/优化预览 + 格式/质量/体积；确认后由调用方写文件或本对话框内存储。
 * 对照 PS Save for Web；GIMP「导出图像」+ 编码器参数。
 */
#ifndef SAVEFORWEBDIALOG_H
#define SAVEFORWEBDIALOG_H

#include "io/rasterio.h"

#include <QDialog>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui {
class SaveForWebDialog;
}
QT_END_NAMESPACE

class QTimer;

/**
 * Photoshop 风格「存储为 Web」对话框。
 * 输入为合成预乘图；不改工程。accept 表示用户点了「存储」且路径已选好。
 */
class SaveForWebDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param premulComposite 当前文档合成结果（ARGB32_Premultiplied）
     * @param suggestedPath   默认保存路径（含文件名，后缀可被格式覆盖）
     */
    explicit SaveForWebDialog(const QImage &premulComposite,
                              const QString &suggestedPath,
                              QWidget *parent = nullptr);
    ~SaveForWebDialog() override;

    Ps::WebExportOptions options() const;
    /** 用户确认存储时选中的路径（accept 后才有意义）。 */
    QString savePath() const { return m_savePath; }

private:
    void showEvent(QShowEvent *event) override;
    void rebuildFormatList();
    void syncControlVisibility();
    void scheduleRefresh();
    void refreshPreviewAndSize();
    void onSaveClicked();
    Ps::WebExportFormat currentFormat() const;
    static QString formatBytes(qint64 bytes);
    static QPixmap fitPreview(const QImage &img, const QSize &box);

    Ui::SaveForWebDialog *ui;
    QImage m_source; ///< 合成预乘原稿
    QString m_suggestedPath;
    QString m_savePath;
    QTimer *m_refreshTimer = nullptr;
    bool m_blockSync = false;
};

#endif // SAVEFORWEBDIALOG_H
