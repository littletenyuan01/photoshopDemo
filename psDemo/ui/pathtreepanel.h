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
 * 【缩略图：刻意不做像素缩略图】
 * 路径是**矢量**，没有可缩放的像素；且 PS 的路径面板本身也不显示缩略图。
 * 但为了让三 Tab 的行高与「图层 / 通道」两栏一致，这里给行加一个**路径标记图标**
 * （`:/icons/paths/new-path.png`），而不是画一条编造的贝塞尔曲线。
 * 真正的路径预览要等 path domain 建起来后，按路径数据描出轮廓（对照
 * GIMP 的 GimpViewRenderer 对 path 的渲染）。
 *
 * 尚无 path 域模型，仅 UI 骨架。
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
