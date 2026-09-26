#ifndef ITEMTREEPANEL_H
#define ITEMTREEPANEL_H

#include <QSize>
#include <QWidget>

class QFrame;
class QIcon;
class QImage;
class QListWidget;
class QToolButton;

namespace Ps {
class ImageDocument;
}

/**
 * 通道缩略图的派生方式（仅 UI 内部用）。
 * 尚无 Channel domain，通道行的缩略图由**合成图实时派生**，属展示层推算。
 */
enum class ThumbChannel {
    Composite, ///< RGB 行：彩色合成缩略图
    Red,       ///< 红分量灰度
    Green,     ///< 绿分量灰度
    Blue,      ///< 蓝分量灰度
    Alpha,     ///< Alpha 灰度（按 PS 习惯反转，白=不透明）
};

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
 *   ItemTreePanel 子类，由 DockPanel（PS 式 Tab 壳）嵌入同一停靠区
 *
 * 【缩略图】三个面板统一在此生成，见 §缩略图 一节。
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

    // —— 缩略图 ——

    /** 缩略图边长（逻辑像素，正方形）。 */
    static constexpr int kThumbSize = 40;

    /** 底栏功能按钮的图标边长（逻辑像素）。 */
    static constexpr int kToolbarIconSize = 24;

    /**
     * 图层缩略图：等比缩放该层像素 + 透明棋盘格衬底。
     * 【对照 GIMP】gimp_viewable_get_preview → GimpViewRenderer 的图层预览。
     */
    static QImage makeLayerThumbnail(const QImage &layerPixels);

    /**
     * 通道缩略图：从**合成图**派生。
     * @param composite 合成结果（Format_ARGB32_Premultiplied）
     *
     * 【诚实标注】当前无 Channel domain，红/绿/蓝/Alpha 都是由合成图算出来的
     * 展示层推算值，不是真实通道数据。等 Phase 后续开建通道 domain 后应换掉。
     */
    static QImage makeChannelThumbnail(const QImage &composite, ThumbChannel channel);

    /**
     * 从 SVG 资源渲染图标，按 1x/2x 双分辨率产出，任意显示尺寸都锐利。
     * 【对照 GIMP】图标是矢量资源；本项目用 Qt6Svg 在目标尺寸上光栅化，
     * 避免「48px PNG 缩到 22px 发糊」的位图问题。
     */
    static QIcon svgIcon(const QString &resourcePath, int logicalSize);

    /**
     * 把按钮改成「线框图标 + 中文 tip」的统一外观。
     * 图层/通道/路径三个子面板本是一致的规则，早先在各自 .cpp 里逐字重复了三遍；
     * 颜色面板、属性面板也要用同一套外观，故与本面板是否为子类无关，对外公开。
     * 【对照 GIMP】gimp_editor_add_icon_box + gimp_editor_set_action_sensitive 的按钮外观约定。
     * @param logicalSize 图标边长（逻辑像素）；默认取列表底栏的 24px，
     *                    更矮的面板底栏（如颜色/属性面板）可传小一号的尺寸。
     */
    static void applyToolbarIcon(QToolButton *button,
                                 const QString &resourcePath,
                                 int logicalSize = kToolbarIconSize);

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
