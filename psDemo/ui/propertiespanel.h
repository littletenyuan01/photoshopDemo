/**
 * propertiespanel.h — 属性/调整/库停靠面板声明（ui 层）。
 *
 * 订阅 AppSession；选中调整层时切换为对应参数页（对照 PS 属性面板）。
 */
#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include <QWidget>

class QToolButton;
class AdjustmentPropsHost;

QT_BEGIN_NAMESPACE
namespace Ui {
class PropertiesPanel;
}
QT_END_NAMESPACE

namespace Ps {
class AppSession;
class ImageDocument;
class FilterNode;
}

/**
 * 属性 / 调整 / 库 停靠面板（ui）。
 *
 * 【对照 GIMP】GIMP **没有**与 PS 这三页一一对应的 dockable；
 * 调整参数在 GIMP 里多经 drawable filter 对话框（点 fx），非常驻属性页。
 * 本面板按 PS：选中调整层 → 显示该类型参数 UI。
 */
class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget *parent = nullptr);
    ~PropertiesPanel() override;

    void setSession(Ps::AppSession *session);

private slots:
    void onSessionDocumentChanged(Ps::ImageDocument *document);
    void onAdjPreview(const Ps::FilterNode &node);
    void onAdjCommit(const Ps::FilterNode &node);

private:
    void bindCollapsible(QToolButton *toggle, QWidget *body);
    void refreshFromDocument();
    void setAdjustmentMode(bool on);

    Ui::PropertiesPanel *ui;
    AdjustmentPropsHost *m_adjHost = nullptr;
    Ps::AppSession *m_session = nullptr;
    Ps::ImageDocument *m_document = nullptr;
};

#endif // PROPERTIESPANEL_H
