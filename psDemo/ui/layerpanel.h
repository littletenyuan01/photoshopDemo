#ifndef LAYERPANEL_H
#define LAYERPANEL_H

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
 * 右侧停靠壳（ui）：图层 | 通道 | 路径。
 *
 * 【对照 GIMP】dialogs-constructors.c 里 layers / channels / paths 是三个独立
 * dockable（gimp-layer-list 等），可被用户叠进同一 notebook。
 * 【外观】参考 PS：固定三 Tab 合一面板。
 *
 * 真正列表逻辑在 LayerTreePanel / ChannelTreePanel / PathTreePanel
 * （对应 GimpLayer/Channel/PathTreeView）。本类只做 Tab 壳与 setDocument 转发。
 */
class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(QWidget *parent = nullptr);
    ~LayerPanel() override;

    /** 转发到三个 ItemTreePanel；不取得所有权。 */
    void setDocument(Ps::ImageDocument *document);

private:
    Ui::LayerPanel *ui;
};

#endif // LAYERPANEL_H
