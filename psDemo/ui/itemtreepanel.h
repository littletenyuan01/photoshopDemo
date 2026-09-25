#ifndef ITEMTREEPANEL_H
#define ITEMTREEPANEL_H

#include <QWidget>

class QFrame;
class QListWidget;
class QToolButton;

namespace Ps {
class ImageDocument;
}

/**
 * Item 树面板基类（ui）。
 *
 * 【对照 GIMP】对应 app/widgets/gimpitemtreeview.*
 * GIMP 骨架：options 区（add_options）+ 树 + 底栏 action 按钮
 * （new / raise / lower / duplicate / delete，由 class 上的 *_action 名绑定）。
 *
 * 本项目瘦身：
 * - 不用 Gtk / UIManager；底栏先直接 connect 槽，后续可再抽 actions 层
 * - Layers / Channels / Paths 在 GIMP 是三个独立 dockable；此处各自为
 *   ItemTreePanel 子类，由 LayerPanel（PS 式 Tab 壳）嵌入同一停靠区
 *
 * 子类须在 setupUi 后调用 bindSkeleton()，再实现 refreshFromDocument()。
 */
class ItemTreePanel : public QWidget
{
    Q_OBJECT

public:
    /** 不取得所有权；nullptr 清空。对应 gimp_item_tree_view_set_image。 */
    void setDocument(Ps::ImageDocument *document);
    Ps::ImageDocument *document() const { return m_document; }

protected:
    explicit ItemTreePanel(QWidget *parent = nullptr);
    ~ItemTreePanel() override;

    /**
     * 挂接 .ui 中的三段骨架（须与 GIMP options / tree / button_box 对应）。
     * 子类 setupUi(this) 之后立刻调用。
     */
    void bindSkeleton(QFrame *optionsHost, QListWidget *itemList, QFrame *toolbarHost);

    QFrame *optionsHost() const { return m_optionsHost; }
    QListWidget *itemList() const { return m_itemList; }
    QFrame *toolbarHost() const { return m_toolbarHost; }

    /** 在底栏左侧追加按钮（对应 gimp_editor_add_action_button 的简化版）。 */
    QToolButton *addToolbarButton(const QString &objectName,
                                  const QString &text,
                                  const QString &toolTip);

    /**
     * 文档指针已更新、旧信号已断开；子类重绑业务信号并 refresh。
     * 基类默认只调 refreshFromDocument()。
     */
    virtual void onDocumentChanged();

    /** 按文档重建列表与顶部控件。 */
    virtual void refreshFromDocument() = 0;

    /** 对应 GIMP new_action / delete_action；默认空实现。 */
    virtual void onNewItem();
    virtual void onDeleteItem();

private:
    Ps::ImageDocument *m_document = nullptr;
    QFrame *m_optionsHost = nullptr;
    QListWidget *m_itemList = nullptr;
    QFrame *m_toolbarHost = nullptr;
};

#endif // ITEMTREEPANEL_H
