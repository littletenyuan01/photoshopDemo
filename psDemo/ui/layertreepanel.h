#ifndef LAYERTREEPANEL_H
#define LAYERTREEPANEL_H

#include "itemtreepanel.h"

#include <QListWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerTreePanel;
}
QT_END_NAMESPACE

namespace Ps {
class Layer;
}

/**
 * 图层树面板（ui）。
 *
 * 【对照 GIMP】app/widgets/gimplayertreeview.*（经 DrawableTreeView）
 * - options：Mode（GimpLayerModeBox）+ Opacity；本项目用 blendModeCombo + opacitySlider
 * - 底栏 action：layers-new / layers-new-group / layers-anchor / merge / mask / delete
 * - 底栏外观：加大按钮 + `:/icons/layers/` 下的线框图标（非字母占位）
 * - 锁：GIMP 有 lock content/position/visibility/alpha；此处 UI 先占位
 *
 * 【更新策略：增量而非重建】
 * 对应 GIMP 中 GimpContainer 的 add/remove/reorder/rename 增量化 notify：
 * - structureChanged        → 重建列表（唯一需要重建的情况）
 * - activeLayerChanged      → 只同步选中行与选项控件
 * - layerPropertiesChanged  → 只更新受影响那一行
 * 这样画笔写像素（pixelsChanged / contentChanged）不会打扰面板的选中与编辑态。
 *
 * 【滑条提交语义】不透明度滑条拖动中只做 UI 预览，松手（sliderReleased）
 * 或键盘操作才写入 domain，使「一次操作 = 一次状态变更」（为撤销铺路）。
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
    void onActiveLayerChanged(int index);
    void onLayerPropertiesChanged(int stackIndex);
    /** 拖动中：只更新百分比文字（预览）。 */
    void onOpacityValueChanged(int value);
    /** 松手：把最终值提交给 domain。 */
    void onOpacityCommitted();

private:
    /** 把滑条数值提交为活动层不透明度；与当前值相同则跳过。 */
    void commitOpacity(int value);
    /** 按 layer 追加一行（含 UserRole 身份映射）。 */
    void appendRowForLayer(int stackIndex, Ps::Layer &layer);
    /** 按栈下标找行（行序会变，不能用行号当身份）。 */
    QListWidgetItem *itemForStackIndex(int stackIndex) const;
    /** 把列表选中行与选项区（不透明度等）对齐到活动层，不重建列表。 */
    void syncActiveRowAndOptions();
    /** 无文档时禁用选项控件。 */
    void setOptionsEnabled(bool enabled);

    Ui::LayerTreePanel *ui;
};

#endif // LAYERTREEPANEL_H
