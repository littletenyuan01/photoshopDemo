/**
 * homescreen.h — PS 主页/启动屏声明（ui 层）。
 *
 * 新文件/打开/最近项网格；对照 GIMP welcome-dialog Create 页。
 */
#ifndef HOMESCREEN_H
#define HOMESCREEN_H

#include <QWidget>

class QFrame;

QT_BEGIN_NAMESPACE
namespace Ui {
class HomeScreen;
}
QT_END_NAMESPACE

/**
 * Photoshop 风格「主页 / 启动屏」。
 *
 * 【对齐 PS】选项条「家」或启动默认进入；左侧新文件/打开，右侧最近使用项网格。
 * 【对照 GIMP】welcome-dialog Create 页；本项目用主窗口栈页。
 *
 * 最近列表来自 Ps::RecentDocuments（QSettings）。
 * 卡片外观：homescreen.ui 的 recentCard / recentEmptyLabel；cpp 只克隆模板并填数据。
 */
class HomeScreen : public QWidget
{
    Q_OBJECT

public:
    explicit HomeScreen(QWidget *parent = nullptr);
    ~HomeScreen() override;

    /** 从 RecentDocuments 重建卡片（进主页时调用）。 */
    void refreshRecent();

signals:
    void newFileRequested();
    void openFileRequested();
    void backToWorkspaceRequested();
    /** 用户点击某最近项；path 为绝对路径。 */
    void recentFileActivated(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clearDynamicCards();
    /** 克隆 .ui 模板 recentCard，填入路径/标题/缩略图（不写死布局数值）。 */
    QFrame *createCard(const QString &path, const QString &title,
                       const QString &subtitle, const QPixmap &thumb);

    Ui::HomeScreen *ui;
};

#endif // HOMESCREEN_H
