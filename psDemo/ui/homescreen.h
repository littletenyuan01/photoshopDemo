#ifndef HOMESCREEN_H
#define HOMESCREEN_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class HomeScreen;
}
QT_END_NAMESPACE

/**
 * Photoshop 风格「主页 / 启动屏」壳。
 *
 * 【对齐 PS】选项条左端「家」按钮进入本页：左侧新文件/打开/主页导航，
 * 右侧「最近使用项」网格（当前为占位卡片）。
 *
 * 【对照 GIMP】`app/dialogs/welcome-dialog.c` 的 Create 页（New / Open +
 * Recent Images）。GIMP 做成模态 Welcome Dialog；本项目按 PS 做成
 * **主窗口内全页切换**（QStackedWidget），更贴截图。
 *
 * ⚠️ UI 阶段：只发信号，不写 document / 最近文件持久化。
 */
class HomeScreen : public QWidget
{
    Q_OBJECT

public:
    explicit HomeScreen(QWidget *parent = nullptr);
    ~HomeScreen() override;

signals:
    /** 用户点「新文件」—— 应由 MainWindow 弹出 NewDocumentDialog。 */
    void newFileRequested();
    /** 用户点「打开」—— 可接到已有打开文件流程。 */
    void openFileRequested();
    /** 顶栏返回箭头：回到编辑工作区。 */
    void backToWorkspaceRequested();

private:
    Ui::HomeScreen *ui;
};

#endif // HOMESCREEN_H
