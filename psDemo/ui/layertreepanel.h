/**
 * layertreepanel.h — 图层树面板声明（ui 层）。
 *
 * 增量更新、缩略图防抖、滑条两段提交；Ctrl+点缩略图 alpha→选区。
 * 每个图层对应一个 LayerRowWidget（.ui），经 setItemWidget 挂到列表行。
 */
#ifndef LAYERTREEPANEL_H
#define LAYERTREEPANEL_H

#include "itemtreepanel.h"
#include "domain/blendmode.h"

#include <QHash>
#include <QListWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerTreePanel;
}
QT_END_NAMESPACE

class QTimer;
class QMouseEvent;
class QPoint;
class QMenu;
class LayerRowWidget;

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
 * - **Ctrl+点缩略图**：图层 alpha → 选区（GIMP 为 Alt+点；本项目对齐 PS Ctrl+点）；
 *   纯 Ctrl 再点同一层 → 取消选区
 * - **右键菜单**：对齐 PS 图层面板弹出项（多数灰显占位）；**复制图层**、
 *   **拷贝/粘贴图层样式**已接 domain（对照 GIMP `layers-duplicate` /
 *   PS Copy/Paste Layer Style）
 *
 * 【图层行】每层 new 一个 LayerRowWidget（眼睛/缩略图/名/fx/展开 + 缩进样式子树），
 * 对照 PS 图层面板缩进「效果」列表；维护时只改该 .ui / 类即可。
 *
 * 【更新策略：增量而非重建】
 * - structureChanged        → 重建列表（唯一需要重建的情况）
 * - activeLayerChanged      → 只同步选中行与选项控件
 * - layerPropertiesChanged  → 只更新受影响那一行
 * - contentChanged          → **防抖**刷新缩略图（见 m_thumbTimer）
 *
 * 【滑条提交语义】不透明度滑条拖动中只做 UI 预览，松手（sliderReleased）
 * 或键盘操作才写入 domain。
 *
 * 列表约定：第 0 行 = 视觉最上层 = LayerStack 最大下标。
 * 拖拽重排：对照 GIMP GimpItemTreeView DnD → gimp_image_reorder_item。
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
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onBtnNewClicked();
    void onBtnDeleteClicked();
    void onBtnLayerStyleClicked();
    void onBtnLayerMaskClicked();
    void onBtnAdjustmentClicked();
    void onListSelectionChanged();
    void onActiveLayerChanged(int index);
    void onLayerPropertiesChanged(int stackIndex);
    void onOpacityValueChanged(int value);
    void onOpacityCommitted();
    void onBlendModeChanged(int index);
    void onBlendModeHighlighted(int index);
    void onThumbnailTimer();
    void onLayerContextMenu(const QPoint &pos);
    void onCopyLayerStyle();
    void onPasteLayerStyle();

private:
    void buildLayerContextMenu();
    void commitOpacity(int value);
    void setupBlendModeCombo();
    int comboIndexForBlendMode(Ps::BlendMode mode) const;
    bool blendModeAtComboIndex(int index, Ps::BlendMode *out) const;
    void beginBlendModePreview();
    void endBlendModePreview();
    void applyBlendModeToActiveLayer(Ps::BlendMode mode);

    /** 新建一层对应的行 widget，挂到列表。 */
    void appendRowForLayer(int stackIndex, Ps::Layer &layer);
    /** 按栈下标取行 widget。 */
    LayerRowWidget *rowWidgetForStackIndex(int stackIndex) const;
    QListWidgetItem *itemForStackIndex(int stackIndex) const;
    void syncItemSize(QListWidgetItem *item, LayerRowWidget *row);
    /** 从 layer 像素生成缩略图并写入行（建行 / 防抖共用）。 */
    void applyLayerThumbnail(LayerRowWidget *row, const Ps::Layer &layer);
    void syncActiveRowAndOptions();
    void setOptionsEnabled(bool enabled);

    void refreshRowThumbnail(int stackIndex, const Ps::Layer *layer = nullptr);
    void scheduleThumbnailRefresh();

    /**
     * Ctrl(+修饰) 点缩略图：图层 alpha → 选区。
     * @return true 表示已处理。
     */
    bool applyAlphaToSelection(int stackIndex, Qt::KeyboardModifiers mods);
    bool applyMaskToSelection(int stackIndex, Qt::KeyboardModifiers mods);

    void syncLayerContextMenuState();

    /**
     * 处理图层行拖放到列表（对照 GIMP get_drop_index + reorder_item）。
     * @return true 已消费该拖放事件。
     */
    bool handleLayerListDrag(QEvent *event);

    Ui::LayerTreePanel *ui;
    QTimer *m_thumbTimer = nullptr;
    QMenu *m_layerContextMenu = nullptr;

    /** 各层「效果」展开态（按栈下标；重建列表时尽量保留）。 */
    QHash<int, bool> m_stylesExpanded;

    int m_alphaSelectSourceLayer = -1;
    int m_maskSelectSourceLayer = -1;
    bool m_settingAlphaSelect = false;

    bool m_blendPreviewActive = false;
    Ps::BlendMode m_blendPreviewOriginal = Ps::BlendMode::Normal;
    int m_blendPreviewLayerIndex = -1;
};

#endif // LAYERTREEPANEL_H
