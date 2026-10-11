/**
 * filterstackwidget.h — 图层滤镜栈面板（ui 层）。
 *
 * 【为什么要有它】滤镜是**非破坏**的层滤镜节点，但在此之前界面上**看不见也管不了**：
 * 给图层应用了三个滤镜之后，既不能单独关掉一个、也不能删除或改参数，只能靠 Ctrl+Z。
 * 本面板把 domain 早已具备的能力（开关 / 删除 / 改参数 / 换序）接出来。
 *
 * 【为什么不新开 dock】功能上它就是「活动图层的属性」，与调整层参数并列放在属性面板里更自然，
 * 也省掉 .ui + 菜单 + 面板注册的一串改动。
 *
 * 【编辑已有滤镜也有实时预览】面板不持有画布，所以预览走「文档里的**当前可见区提示**」
 * （`ImageDocument::setPreviewRegionHint`，由画布在更新视口时写入；`region` 传空即回落到它），
 * 再按 80ms 节流。取消时用整层标脏把原参数写回去（预览只刷过可见区，屏幕外还是预览值）。
 */
#ifndef UI_FILTERSTACKWIDGET_H
#define UI_FILTERSTACKWIDGET_H

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Ps {
class ImageDocument;
}

class FilterStackWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FilterStackWidget(QWidget *parent = nullptr);

    /** 绑定文档与图层下标；没有可用像素层时自动隐藏。 */
    void setContext(Ps::ImageDocument *doc, int layerIndex);
    /** 从文档重新拉取（撤销等外部改动之后调用）。 */
    void refresh();

signals:
    /** 用户改动了滤镜栈（供上层刷新其它视图）。 */
    void stackChanged();

private:
    void onItemChanged(QListWidgetItem *item);
    void onDelete();
    void onEdit();
    void onMove(int delta);
    int currentIndex() const;
    /** 按当前选中行刷新按钮可用性。**选中变化时也要调**，否则「上移」会一直灰着。 */
    void updateButtons();

    Ps::ImageDocument *m_doc = nullptr;
    int m_layer = -1;
    bool m_block = false; ///< 重建列表时屏蔽 itemChanged
    QListWidget *m_list = nullptr;
    QPushButton *m_editBtn = nullptr;
    QPushButton *m_delBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;
};

#endif // UI_FILTERSTACKWIDGET_H
