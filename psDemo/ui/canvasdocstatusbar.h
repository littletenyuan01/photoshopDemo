#ifndef CANVASDOCSTATUSBAR_H
#define CANVASDOCSTATUSBAR_H

#include <QWidget>

namespace Ps {
class ImageDocument;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class CanvasDocStatusBar;
}
QT_END_NAMESPACE

/**
 * 画布底部状态条（ui）：缩放% + 文档信息 + 菜单箭头。
 *
 * 布局：canvasdocstatusbar.ui。
 * 对齐 Photoshop 文档窗口底栏左侧（水平滚动条左侧）；
 * GIMP 对应 display 的 GimpStatusbar（含 scale combo），本项目瘦身为 PS 外观。
 */
class CanvasDocStatusBar : public QWidget
{
    Q_OBJECT

public:
    enum class InfoMode {
        DocumentSize, // 物理尺寸 + ppi（默认，对齐截图）
        PixelSize,    // 仅像素
    };

    explicit CanvasDocStatusBar(QWidget *parent = nullptr);
    ~CanvasDocStatusBar() override;

public slots:
    void setZoomFactor(qreal zoom); // 1.0 = 100%
    void setDocument(Ps::ImageDocument *document);

signals:
    /** 用户在缩放框回车后发出；1.0 = 100%。 */
    void zoomCommitted(qreal zoom);

private slots:
    void onZoomEditingFinished();
    void onShowDocumentSize();
    void onShowPixelSize();

private:
    void refreshDocInfo();
    void setupInfoMenu();

    Ui::CanvasDocStatusBar *ui;
    Ps::ImageDocument *m_document = nullptr;
    InfoMode m_infoMode = InfoMode::DocumentSize;
    /** 文档尚无独立分辨率字段时，显示用默认 ppi（对齐常见 PS 新建）。 */
    qreal m_displayPpi = 72.0;
    bool m_updatingZoomText = false;
};

#endif // CANVASDOCSTATUSBAR_H
