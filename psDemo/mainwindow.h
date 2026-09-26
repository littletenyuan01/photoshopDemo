#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "tools/toolid.h"

#include <QMainWindow>

class QCloseEvent;
class QResizeEvent;

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

protected:
    /**
     * 首次拿到真实窗口高度后，给右侧栏三段分配默认高度比例。
     * 【为什么不能只在构造里 setSizes】构造期 splitter 高度还没定，
     * 传进去的比例会被「最后一段吃掉差额」的规则压扁（实测 250/240/460 → 250/240/245，
     * 结果图层面板反而最小）。真实高度只有 resize 之后才知道。
     * 因此每次 resize 都按比例重算，**直到用户自己拖过分隔条**为止。
     */
    void resizeEvent(QResizeEvent *event) override;

    /**
     * 退出前确认（对齐 PS）。所有关闭路径都汇到这里：
     * 右上角 ×、文件→退出、Alt+F4，以及**标题栏 logo 双击**
     * （Windows 把它变成 SC_CLOSE，Qt 统一转成 QCloseEvent）。
     */
    void closeEvent(QCloseEvent *event) override;

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
    /** 按当前高度给右侧栏三段分配默认比例（颜色 26% / 属性 24% / 图层 50%）。 */
    void applyDefaultRightColumnSizes();

    Ui::MainWindow *ui;
    Ps::AppSession *m_session = nullptr;
    bool m_rightColumnUserSized = false; ///< 用户拖过分隔条后，不再套用默认比例
    bool m_closeConfirming = false;      ///< 正在弹退出确认框（防重入）
};

#endif // MAINWINDOW_H
