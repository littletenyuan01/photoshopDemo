#ifndef PATHTREEPANEL_H
#define PATHTREEPANEL_H

#include "itemtreepanel.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class PathTreePanel;
}
QT_END_NAMESPACE

/**
 * 路径树面板（ui）。
 *
 * 【对照 GIMP】app/widgets/gimppathtreeview.*（直接挂 ItemTreeView，无 Drawable 层）
 * - 底栏：to-selection / selection-to-path / stroke + new / delete
 * - 底栏外观：加大按钮 + `:/icons/paths/` 下的 png（删除共用 layers/delete）
 * - 无 Mode/Opacity options（与 Layer 不同）
 *
 * 外观参考 PS 路径面板；尚无 path 域模型，仅 UI 骨架。
 */
class PathTreePanel : public ItemTreePanel
{
    Q_OBJECT

public:
    explicit PathTreePanel(QWidget *parent = nullptr);
    ~PathTreePanel() override;

protected:
    void refreshFromDocument() override;
    void onNewItem() override;
    void onDeleteItem() override;

private:
    Ui::PathTreePanel *ui;
};

#endif // PATHTREEPANEL_H
