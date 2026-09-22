#ifndef CANVASWORKSPACE_H
#define CANVASWORKSPACE_H

#include <QWidget>

class CanvasView;

QT_BEGIN_NAMESPACE
namespace Ui {
class CanvasWorkspace;
}
QT_END_NAMESPACE

/**
 * 画布工作区（ui）：标尺 + CanvasView + 底栏状态 + 滚动条。
 *
 * 布局：canvasworkspace.ui（对齐 Photoshop 画布区）。
 * 底栏左侧为缩放%/文档信息（CanvasDocStatusBar），右侧为水平滚动条。
 */
class CanvasWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWorkspace(QWidget *parent = nullptr);
    ~CanvasWorkspace() override;

    CanvasView *canvasView() const;
    /** 同步文档信息到状态条（MainWindow 换文档时调用）。 */
    void notifyDocumentChanged();

private slots:
    void syncRulersAndScrollBars();
    void syncDocStatus();
    void onCanvasMouseMoved(const QPointF &imagePos, bool inside);
    void onHScroll(int value);
    void onVScroll(int value);
    void onStatusZoomCommitted(qreal zoom);

private:
    Ui::CanvasWorkspace *ui;
    bool m_updatingScrollBars = false;
};

#endif // CANVASWORKSPACE_H
