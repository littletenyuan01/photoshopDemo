/**
 * historypanel.h — 历史面板：线性历史列表 + 任意跳转（ui 层）。
 */
#ifndef HISTORYPANEL_H
#define HISTORYPANEL_H

#include <QWidget>

namespace Ps {
class AppSession;
class ImageDocument;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class HistoryPanel;
}
QT_END_NAMESPACE

class HistoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget *parent = nullptr);
    ~HistoryPanel() override;

    /** 与其它面板同法：接 AppSession，跟随 documentChanged。 */
    void setSession(Ps::AppSession *session);

private slots:
    void refreshFromDocument();
    void onListSelectionChanged();
    void onStepBack();
    void onStepForward();
    void onNewBookmark();
    void onDeleteBookmark();

private:
    void bindDocument(Ps::ImageDocument *document);
    void jumpWithUiGuard(int step);

    Ui::HistoryPanel *ui = nullptr;
    Ps::AppSession *m_session = nullptr;
    Ps::ImageDocument *m_document = nullptr;
    bool m_updating = false; ///< 程序化刷新期间屏蔽 currentRowChanged
};

#endif // HISTORYPANEL_H
