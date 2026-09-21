#ifndef LAYERPANEL_H
#define LAYERPANEL_H

#include <QListWidgetItem>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerPanel;
}
QT_END_NAMESPACE

namespace Ps {
class ImageDocument;
}

/**
 * 图层面板（ui）。
 *
 * 布局：layerpanel.ui（可在 Qt Designer 中调整）。
 * 数据：不拥有 ImageDocument，只借用指针并通过文档 API 改层。
 *
 * 列表约定（与 LayerStack 相反，贴近常见修图软件习惯）：
 * - 列表第 0 行 = 视觉最上层 = LayerStack 最大下标
 * - LayerStack index 0 = 最底层（合成时先画）
 *
 * 【待对照 GIMP】当前实现未逐项对照 gimp-master 的 layers dock
 * （如 app/widgets 中图层树/停靠面板）。下次改版须按 GIMP 职责再收敛，
 * 不可继续凭印象扩展。
 */
class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(QWidget *parent = nullptr);
    ~LayerPanel() override;

    /** 不取得所有权；nullptr 清空面板。 */
    void setDocument(Ps::ImageDocument *document);

private slots:
    void onAddLayer();
    void onDeleteLayer();
    void onMoveUp();
    void onMoveDown();
    void onListSelectionChanged();
    /** 勾选 → 显隐；编辑文本 → 重命名。 */
    void onItemChanged(QListWidgetItem *item);
    void onOpacityChanged(int value);
    /** 根据文档重建列表与滑条（结构/活动层变化时）。 */
    void refreshFromDocument();

private:
    /** 列表行号 → LayerStack 下标。 */
    int stackIndexFromRow(int row) const;
    /** LayerStack 下标 → 列表行号。 */
    int rowFromStackIndex(int stackIndex) const;
    /** 重建 UI 时阻断信号，避免回写文档造成循环。 */
    void blockUiSignals(bool block);

    Ui::LayerPanel *ui;
    Ps::ImageDocument *m_document = nullptr;
};

#endif // LAYERPANEL_H
