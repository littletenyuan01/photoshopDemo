#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "tools/toolid.h"

#include <QMainWindow>

namespace Ps {
class AppSession;
class ImageDocument;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * 主窗口壳（ui）。
 * 布局与菜单在 mainwindow.ui。
 *
 * 整体布局对齐 Photoshop：
 *   顶：菜单栏 → 工具选项栏
 *   中：左侧工具箱 | 标尺+画布工作区 | 右侧图层/通道/路径面板
 *
 * 【职责收窄】本类只做两件事：
 * 1. **菜单/动作接线**（action → 槽）
 * 2. **装配与广播**：持有 AppSession，把 session 交给各组件，
 *    之后文档变化由 AppSession 广播，**本类不再手工逐个 setDocument**。
 *
 * 早先换文档要连续调用 canvasWorkspace/canvasView/layerPanel/docStatusBar
 * 四处 setDocument，加一个面板就得加一行，漏一行即静默不刷新（见 docs/ui-review.md）。
 *
 * 工具箱结构参考 GIMP GimpToolbox（按钮区 + 前/背景色）。
 * 标尺工作区参考 GIMP display shell（hrule/vrule + canvas）。
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
    void onToggleDockPanel(bool visible);
    void onAbout();
    void onToolChanged(Ps::ToolId id);
    void onBrushDiameterChanged(int diameter);
    void onForegroundColorChanged(const QColor &color);
    void onBackgroundColorChanged(const QColor &color);

private:
    /** 装配菜单动作连接。 */
    void setupMenus();
    /** 把 session 交给画布工作区与右侧面板（一次性，之后靠广播）。 */
    void setupSession();
    /** 连接工具箱 ↔ 选项栏 ↔ 画布。 */
    void setupToolbox();
    /** 载入一篇默认文档，避免启动即空白壳。 */
    void createInitialDocument();
    void updateWindowTitle(Ps::ImageDocument *document);

    Ui::MainWindow *ui;
    Ps::AppSession *m_session = nullptr;
};

#endif // MAINWINDOW_H
