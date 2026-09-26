#ifndef DOCKPANEL_H
#define DOCKPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerPanel;
}
QT_END_NAMESPACE

namespace Ps {
class AppSession;
class ImageDocument;
}

/**
 * 右侧停靠壳（ui）：图层 | 通道 | 路径 三个 Tab。
 *
 * 【为什么叫 DockPanel 而不是 LayerPanel】
 * 本类**不含任何图层逻辑**，它只是 PS 式三 Tab 停靠容器。
 * 真正的列表在 LayerTreePanel / ChannelTreePanel / PathTreePanel 里
 * （对应 GimpLayer/Channel/PathTreeView）。早先命名为 LayerPanel，
 * 与 LayerTreePanel 只差一个词，极易混淆，故改名。
 * （.ui 文件名保留 layerpanel.ui，避免与 Designer 反复来回改。）
 *
 * 【对照 GIMP】dialogs-constructors.c 里 layers / channels / paths 是三个独立
 * dockable（gimp-layer-list 等），可被用户叠进同一 notebook；
 * 本项目按 PS 外观固定成三 Tab。
 *
 * 【文档来源】订阅 Ps::AppSession::documentChanged，不再由 MainWindow 手工转发。
 * 新增面板时只要在这里多转发一次，MainWindow 无需改动。
 */
class DockPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DockPanel(QWidget *parent = nullptr);
    ~DockPanel() override;

    /** 订阅会话的文档广播；不取得所有权。 */
    void setSession(Ps::AppSession *session);

private slots:
    /** 会话换文档时转发给三个子面板。 */
    void onSessionDocumentChanged(Ps::ImageDocument *document);

private:
    Ui::LayerPanel *ui;
    Ps::AppSession *m_session = nullptr;
};

#endif // DOCKPANEL_H
