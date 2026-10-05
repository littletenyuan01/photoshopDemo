/**
 * layerrowwidget.h — 单个图层条目 UI（ui）。
 *
 * 对照 PS 图层面板一行：眼睛 / 缩略图 / 名称 / fx / 展开箭头，
 * 下方缩进挂「效果」组头与各样式子行。每新建一层 new 一个本对象。
 */
#ifndef LAYERROWWIDGET_H
#define LAYERROWWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LayerRowWidget;
}
QT_END_NAMESPACE

class QImage;
class QMouseEvent;
class QLineEdit;

namespace Ps {
class Layer;
}

class LayerRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LayerRowWidget(QWidget *parent = nullptr);
    ~LayerRowWidget() override;

    /** 按图层数据刷新行（名称、显隐、样式子树；不含缩略图）。 */
    void syncFromLayer(const Ps::Layer &layer);
    /** 只换缩略图（画笔防抖 / 建行路径）。 */
    void setThumbnail(const QImage &thumb);
    /** 图层蒙版缩略图；空图则隐藏。 */
    void setMaskThumbnail(const QImage &thumb);
    /**
     * 编辑目标高亮：0=像素，1=蒙版（对照 PS 缩略图白边）。
     * 无蒙版时 mask 高亮忽略。
     */
    void setEditTarget(int target);
    void setExpanded(bool on);
    /** 右键「重命名」入口。 */
    void beginRename();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void visibilityToggled(bool visible);
    void nameCommitted(const QString &name);
    void expandChanged(bool expanded);
    void styleVisibilityToggled(int styleIndex, bool visible);
    void heightChanged();
    void rowPressed();
    void thumbnailCtrlClicked(Qt::KeyboardModifiers mods);
    /** 单击图层缩略图：切到像素编辑。 */
    void layerThumbClicked();
    void maskCtrlClicked(Qt::KeyboardModifiers mods);
    /** Alt 点蒙版缩略图：启用/停用（对照 PS）。 */
    void maskAltClicked();
    /** 单击蒙版缩略图：切到蒙版编辑。 */
    void maskThumbClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** 按当前展开态计算行高，并通知列表更新 sizeHint。 */
    void applyHeight();
    void clearStyleRows();
    void rebuildStyleRows(const Ps::Layer &layer);
    /** 样式数量未变时只刷新眼睛/标题，避免整树销毁重建。 */
    void updateStyleRowsInPlace(const Ps::Layer &layer);
    void updateExpandChrome(bool hasStyles);
    void updateEyeIcon(bool visible);
    void finishRename();

    /** 效果区目标高度（组头 + 子行），不依赖 isVisible()（未入屏时 isVisible 恒 false）。 */
    int stylesBlockHeight() const;

    Ui::LayerRowWidget *ui;
    bool m_expanded = true;
    bool m_hasStyles = false;
    QString m_layerName; ///< 真实层名（不含「链接」后缀）
    QLineEdit *m_renameEdit = nullptr;
};

#endif // LAYERROWWIDGET_H
