#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include <QWidget>

class QToolButton;

QT_BEGIN_NAMESPACE
namespace Ui {
class PropertiesPanel;
}
QT_END_NAMESPACE

namespace Ps {
class AppSession;
class ImageDocument;
}

/**
 * 属性 / 调整 / 库 停靠面板（ui）。
 *
 * 【对照 GIMP】GIMP **没有**与 PS 这三页一一对应的 dockable：
 * - 属性的「变换」区 ≈ GimpTransformTool 的 tool options + GimpItem 的位置尺寸属性
 * - 属性的折叠分区 ≈ app/widgets/gimppropwidgets.c 的 gimp_prop_expanding_frame_new
 * - 调整 ≈ GIMP 的「颜色 > ...」各操作（GEGL operation），不是常驻面板
 * - 库 ≈ GIMP 的 pattern / gradient / brush 资源工厂视图（GimpDataFactoryView）
 * 因此本面板按 PS 截图的外观实现，仅在能对上的地方标注 GIMP 出处，
 * 不硬套 GIMP 结构（见 docs/code-map.md 的说明）。
 *
 * 【数据诚实性】本面板只显示**真实存在**的数据：
 * 文档宽高来自 ImageDocument，图层名来自 Layer。
 * 「X / Y / 旋转」在 domain 里还没有对应属性（Layer 无 offset、无变换矩阵），
 * 故一律标为 UI 占位（见 .ui 的 toolTip），不伪造数值。
 *
 * 【文档来源】订阅 Ps::AppSession::documentChanged（与 DockPanel 同一套广播），
 * 新增面板无需改 MainWindow。
 */
class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget *parent = nullptr);
    ~PropertiesPanel() override;

    /** 订阅会话的文档广播；不取得所有权。 */
    void setSession(Ps::AppSession *session);

private slots:
    /** 会话换文档：重绑文档信号并立即刷新一次。 */
    void onSessionDocumentChanged(Ps::ImageDocument *document);

private:
    /** 分区折叠：切换箭头并显隐内容体。 */
    void bindCollapsible(QToolButton *toggle, QWidget *body);
    /** 把真实数据写进各控件。 */
    void refreshFromDocument();

    Ui::PropertiesPanel *ui;
    Ps::AppSession *m_session = nullptr;
    Ps::ImageDocument *m_document = nullptr;
};

#endif // PROPERTIESPANEL_H
