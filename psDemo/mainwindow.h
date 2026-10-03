/**
 * mainwindow.h — 主窗口壳：菜单接线与会话装配（app 层）。
 *
 * 布局在 mainwindow.ui；文档切换经 AppSession 广播，不再逐个 setDocument。
 */
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
 *   另：选项条「家」→ HomeScreen 全页（对照 GIMP welcome-dialog Create 页，本项目做成栈页）
 *
 * 【职责收窄】本类只做两件事：
 * 1. **菜单/动作接线**（action → 槽）
 * 2. **装配与广播**：持有 AppSession，把 session 交给各组件，
 *    之后文档变化由 AppSession 广播，**本类不再手工逐个 setDocument**。
 *
 * 早先换文档要连续调用 canvasWorkspace/canvasView/layerPanel/docStatusBar
 * 四处 setDocument，加一个面板就得加一行，漏一行即静默不刷新（见 docs/ui/ui-review.md）。
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
    // —— 文件 / 编辑 / 图层 / 图像 / 视图 / 窗口 / 帮助（菜单 action 槽）——
    void onNewDocument();
    void onOpenDocument();
    void onSaveDocument();
    void onSaveDocumentAs();
    void onExportPng();
    void onExportAs();
    void onNewLayer();
    void onDuplicateLayer();
    void onDeleteLayer();
    void onImageSize();
    void onCanvasSize();
    void onShowHomeScreen();
    void onShowWorkspace();
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
    void onSelectAll();
    void onSelectDeselect();
    void onSelectInverse();
    void onClear();
    void onFill();
    void onFreeTransform();
    void onBrightnessContrast();
    void onEnsureLayerStyle(int kind);
    void onClearLayerStyles();
    void onCopyLayerStyles();
    void onPasteLayerStyles();
    void onUndo();
    void onRedo();
    void updateUndoRedoActions();
    void onDocumentChanged(Ps::ImageDocument *doc);
    /** 状态栏/标题：未保存路径前加 *；平时常驻显示路径。 */
    void refreshDocumentPathStatus();
    /** 短暂提示后恢复路径显示（避免「已存储」一直留在状态栏）。 */
    void flashStatusMessage(const QString &message, int ms = 4000);

    /** 自由变换：选项栏数值同步 / 右键菜单 / 模式。 */
    void syncFreeTransformOptionsBar();
    void onFreeTransformContextMenu(const QPoint &widgetPos);
    void applyFreeTransformMode(int mode);

private:
    /** 装配菜单动作连接。 */
    void setupMenus();
    /** 按扩展名保存（.pslite / .psd）；成功则清脏并记 filePath。 */
    bool saveDocumentTo(const QString &path);
    /**
     * 弹出「存储为」：可选 .pslite（完整工程）或 .psd（子集）。
     * @param forcePslite true 时默认/偏向工程格式（供 Ctrl+S 无路径时用）
     */
    bool saveDocumentAsDialog(bool forcePslite = false);
    bool exportCompositeTo(const QString &path);
    QString suggestExportPath(const QString &suffix) const;
    /**
     * 打开路径（.pslite / 栅格）；成功则记入最近、切工作区。
     * @return 是否打开成功
     */
    bool openPath(const QString &path);
    /** 把当前文档合成图写入最近项缩略图缓存。 */
    void rememberRecent(const QString &path);
    /** 按 RecentDocuments 重建「打开最近的文件」子菜单。 */
    void rebuildRecentMenu();
    /** 把 session 交给画布工作区与右侧面板（一次性，之后靠广播）。 */
    void setupSession();
    /** 连接工具箱 ↔ 选项栏 ↔ 画布；以及「家」→ 主页。 */
    void setupToolbox();
    /**
     * 用 setupUi 已建好的 mainStack（工作区 / HomeScreen）接线。
     * 栈结构在 mainwindow.ui；此处只连信号。
     */
    void setupHomeStack();
    /** 载入一篇默认文档，避免启动即空白壳。 */
    void createInitialDocument();
    /** 按当前高度给右侧栏三段分配默认比例（颜色 26% / 属性 24% / 图层 50%）。 */
    void applyDefaultRightColumnSizes();

    Ui::MainWindow *ui;
    Ps::AppSession *m_session = nullptr; ///< 当前文档持有者与广播中心
    QMetaObject::Connection m_historyConn;   ///< 活动文档 HistoryStack::changed
    QMetaObject::Connection m_docStatusConn; ///< 活动文档 contentChanged → 标题/状态栏
    bool m_rightColumnUserSized = false; ///< 用户拖过分隔条后，不再套用默认比例
    bool m_closeConfirming = false;      ///< 正在弹退出确认框（防重入）
};

#endif // MAINWINDOW_H
