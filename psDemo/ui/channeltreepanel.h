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
 * 外观参考 PS 通道面板；尚无 domain 通道模型，仅 UI 骨架。
 */
class ChannelTreePanel : public ItemTreePanel
{
    Q_OBJECT

public:
    explicit ChannelTreePanel(QWidget *parent = nullptr);
    ~ChannelTreePanel() override;

protected:
    void refreshFromDocument() override;
    void onNewItem() override;
    void onDeleteItem() override;

private:
    Ui::ChannelTreePanel *ui;
};

#endif // CHANNELTREEPANEL_H
