#ifndef CHANNELTREEPANEL_H
#define CHANNELTREEPANEL_H

#include "itemtreepanel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ChannelTreePanel;
}
QT_END_NAMESPACE

class QTimer;
class QShowEvent;

/**
 * 通道树面板（ui）。
 *
 * 【对照 GIMP】app/widgets/gimpchanneltreeview.*
 * - 顶部可嵌 GimpComponentEditor（RGB 分量）；本项目暂用占位列表
 * - 底栏：channels-selection-replace + new / delete（见 channels-actions）
 * - 底栏外观：加大按钮 + `:/icons/channels/` 下的 SVG（删除共用 layers/delete）
 *
 * 【缩略图】每行左侧显示缩略图：RGB 行为彩色合成图，红/绿/蓝/Alpha 行为
 * 由合成图实时派生的灰度分量图（Alpha 按 PS 习惯反转，白 = 不透明）。
 *
 * 【刷新策略 · 三条都是实测逼出来的】
 * 1. **防抖 250ms**：`contentChanged` 在画笔拖动时每帧都发，同步刷新会拖垮绘制。
 * 2. **增量更新**：像素变化只换各行的图标，**不 clear() 重建列表**
 *    —— 早先每次画笔移动都会把通道选中项打回第 0 行。
 * 3. **不可见时跳过**：通道面板平时在 DockPanel 的「图层」Tab 后面不可见，
 *    此时不做任何计算，只记一个 dirty 标记，等 `showEvent` 再刷新。
 *    （实测 4000×3000 文档下单次刷新 216 ms，其中大头来自整图分配，见 makeChannelThumbnail）
 *
 * 【诚实标注】尚无 Channel domain，分量均由 `engine/Compositor` 从合成图推算，
 * 属**展示层推算值**，不是真实通道数据；等通道 domain 开建后应改为读取真实通道。
 *
 * 外观参考 PS 通道面板。
 */
class ChannelTreePanel : public ItemTreePanel
{
    Q_OBJECT

public:
    explicit ChannelTreePanel(QWidget *parent = nullptr);
    ~ChannelTreePanel() override;

protected:
    void onDocumentChanged() override;
    void refreshFromDocument() override;
    void onNewItem() override;
    void onDeleteItem() override;
    /** 面板重新可见时，若期间有像素变化则补一次刷新。 */
    void showEvent(QShowEvent *event) override;

private slots:
    /** 防抖定时器到期：只换图标，不重建列表。 */
    void onThumbnailTimer();

private:
    /** 请求防抖刷新：连发多次 contentChanged 只算一次。 */
    void scheduleThumbnailRefresh();
    /** 重算并替换各行的缩略图（不碰文字 / 勾选 / 选中态）。 */
    void updateThumbnails();

    Ui::ChannelTreePanel *ui;
    /** 缩略图防抖定时器（与 LayerTreePanel 同一策略）。 */
    QTimer *m_thumbTimer = nullptr;
    /** 不可见期间发生过像素变化，等显示时补刷新。 */
    bool m_thumbDirty = false;
};

#endif // CHANNELTREEPANEL_H
