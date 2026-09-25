#ifndef LAYERTREEPANEL_H
#define LAYERTREEPANEL_H

#include "itemtreepanel.h"

#include <QListWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerTreePanel;
}
QT_END_NAMESPACE

/**
 * 图层树面板（ui）。
 *
 * 【对照 GIMP】app/widgets/gimplayertreeview.*（经 DrawableTreeView）
 * - options：Mode（GimpLayerModeBox）+ Opacity；本项目用 blendModeCombo + opacitySlider
 * - 底栏 action：layers-new / layers-new-group / layers-anchor / merge / mask / delete
 * - 锁：GIMP 有 lock content/position/visibility/alpha；此处 UI 先占位
 *
 * 列表约定：第 0 行 = 视觉最上层 = LayerStack 最大下标。
 * 外观（筛选行、填充等）参考 PS 图层面板，结构跟 GIMP。
 */
class LayerTreePanel : public ItemTreePanel
{
    Q_OBJECT

public:
    explicit LayerTreePanel(QWidget *parent = nullptr);
    ~LayerTreePanel() override;

protected:
    void onDocumentChanged() override;
    void refreshFromDocument() override;
    void onNewItem() override;
    void onDeleteItem() override;

private slots:
    void onListSelectionChanged();
    void onItemChanged(QListWidgetItem *item);
    void onOpacityChanged(int value);

private:
    int stackIndexFromRow(int row) const;
    int rowFromStackIndex(int stackIndex) const;
    void blockUiSignals(bool block);

    Ui::LayerTreePanel *ui;
};

#endif // LAYERTREEPANEL_H
