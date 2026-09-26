#ifndef CHANNELTREEPANEL_H
#define CHANNELTREEPANEL_H

#include "itemtreepanel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class ChannelTreePanel;
}
QT_END_NAMESPACE

/**
 * 通道树面板（ui）。
 *
 * 【对照 GIMP】app/widgets/gimpchanneltreeview.*
 * - 顶部可嵌 GimpComponentEditor（RGB 分量）；本项目暂用占位列表
 * - 底栏：channels-selection-replace + new / delete（见 channels-actions）
 * - 底栏外观：加大按钮 + `:/icons/channels/` 下的 png（删除共用 layers/delete）
 *
 * 【缩略图】每行左侧显示缩略图：RGB 行为彩色合成图，红/绿/蓝/Alpha 行为
 * 由合成图实时派生的灰度分量图（Alpha 按 PS 习惯反转，白 = 不透明）。
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

private:
    Ui::ChannelTreePanel *ui;
};

#endif // CHANNELTREEPANEL_H
