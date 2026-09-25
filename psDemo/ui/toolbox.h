#ifndef TOOLBOX_H
#define TOOLBOX_H

#include "tools/toolid.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QMenu;
class QToolButton;

QT_BEGIN_NAMESPACE
namespace Ui {
class ToolBox;
}
QT_END_NAMESPACE

/**
 * 左侧工具箱（ui）。
 *
 * 结构参考 GIMP GimpToolbox（按钮区 + 前/背景色）与 Photoshop 左栏：
 * - 相关工具共用一个「占位」按钮（如形状：矩形/椭圆/三角/直线）
 * - 左键：激活该占位当前工具
 * - 右键：弹出同组全部工具（对齐用户提供的 PS 飞出菜单图）
 *
 * 前/背景色区参考 GIMP GimpFgBgEditor：背景右下、前景左上重叠；
 * 切换颜色只交换色值，方块前后关系不变（前景始终在上）。
 */
class ToolBox : public QWidget
{
    Q_OBJECT

public:
    explicit ToolBox(QWidget *parent = nullptr);
    ~ToolBox() override;

    Ps::ToolId currentTool() const { return m_currentTool; }
    QColor foregroundColor() const { return m_fg; }
    QColor backgroundColor() const { return m_bg; }

public slots:
    void setCurrentTool(Ps::ToolId id);

signals:
    void toolChanged(Ps::ToolId id);
    void foregroundColorChanged(const QColor &color);
    void backgroundColorChanged(const QColor &color);

private:
    /** 组内一项：工具 id + 资源路径 + 显示名。 */
    struct ToolItem {
        Ps::ToolId id;
        QString iconPath;
        QString title;
        QString shortcut; // 菜单右侧显示，可空
    };

    /** 工具栏一个占位（可含多个 ToolItem）。 */
    struct ToolSlot {
        QVector<ToolItem> items;
        int activeIndex = 0;
        QToolButton *button = nullptr;
    };

private slots:
    void onSlotClicked(int slotIndex);
    void onPickForeground();
    void onPickBackground();
    void onSwapColors();
    void onDefaultColors();

private:
    void buildToolSlots();
    void addSlot(const QVector<ToolItem> &items);
    void refreshSlotButton(int slotIndex);
    void showSlotMenu(int slotIndex, const QPoint &globalPos);
    int findSlotIndex(Ps::ToolId id) const;
    void updateColorButtons();
    QIcon loadIcon(const QString &path) const;

    Ui::ToolBox *ui;
    QButtonGroup *m_slotGroup = nullptr;
    QVector<ToolSlot> m_slots;
    Ps::ToolId m_currentTool = Ps::ToolId::Move;
    QColor m_fg {Qt::black};
    QColor m_bg {Qt::white};
};

#endif // TOOLBOX_H
