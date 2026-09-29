#ifndef HOMESCREEN_H
#define HOMESCREEN_H

#include <QWidget>

class QFrame;
class QLabel;

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
 * 最近列表来自 Ps::RecentDocuments（QSettings）；.ui 中 recentCard 仅作样式模板。
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
    QFrame *createCard(const QString &path, const QString &title,
                       const QString &subtitle, const QPixmap &thumb);

    Ui::HomeScreen *ui;
    QLabel *m_emptyLabel = nullptr;
};

#endif // HOMESCREEN_H
