#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "tools/toolid.h"

#include <QMainWindow>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

namespace Ps {
class ImageDocument;
}

/**
 * 主窗口壳（ui）。
 * 布局与菜单在 mainwindow.ui。
 *
 * 整体布局对齐 Photoshop：
 *   顶：菜单栏 → 工具选项栏
 *   中：左侧工具箱 | 画布 | 右侧图层面板
 *
 * 工具箱结构参考 GIMP GimpToolbox（按钮区 + 前/背景色）。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNewDocument();
    void onOpenDocument();
    void onZoomFit();
    void onZoomActual();
    void onZoomIn();
    void onZoomOut();
    void onToggleLayerPanel(bool visible);
    void onAbout();
    void onToolChanged(Ps::ToolId id);
    void onBrushDiameterChanged(int diameter);
    void onForegroundColorChanged(const QColor &color);
    void onBackgroundColorChanged(const QColor &color);

private:
    void setDocument(std::unique_ptr<Ps::ImageDocument> document);
    void updateWindowTitle();
    void setupMenus();
    /** 连接工具箱 ↔ 选项栏（以及后续 Canvas 工具路由）。 */
    void setupToolbox();

    Ui::MainWindow *ui;
    std::unique_ptr<Ps::ImageDocument> m_document;
};
#endif // MAINWINDOW_H
