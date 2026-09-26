#ifndef COLORSPANEL_H
#define COLORSPANEL_H

#include <QWidget>

class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

QT_BEGIN_NAMESPACE
namespace Ui {
class ColorsPanel;
}
QT_END_NAMESPACE

/**
 * 颜色 / 色板 / 渐变 / 图案 停靠面板（ui）。
 *
 * 【外观对照】Adobe Photoshop 右侧「颜色」停靠组四页 Tab（用户提供的截图）：
 * - 颜色：重叠前景/背景方块 + 二维 S/V 色域 + 竖直色相条（`HsvColorWell`）
 * - 色板：搜索 + 最近色条 + 可折叠分组，组下为色块网格 + 底栏（组/加/删）
 * - 渐变：搜索 + 分组方缩略图 + 底栏
 * - 图案：搜索 + 分组方缩略图 + 底栏
 *
 * 【对照 GIMP】四者本是独立 dockable；本项目按 PS 收进同一停靠区。
 *
 * 【阶段】以 UI 为主：色板/渐变/图案数据为占位；颜色页 RGB↔色域自洽联动，
 * 不写 document（尚无前景色 domain）。
 */
class ColorsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorsPanel(QWidget *parent = nullptr);
    ~ColorsPanel() override;

private slots:
    void onRgbChanged();
    void onHexEdited();
    void onWellColorChanged(const QColor &color);
    void onSwatchSearchChanged(const QString &text);
    void onGradientSearchChanged(const QString &text);
    void onPatternSearchChanged(const QString &text);

private:
    void buildSwatchGroups();
    void buildGradientGroups();
    void buildPatternGroups();
    /** 把色域当前的前景/背景色写进顶部「最近色」条。 */
    void syncRecentStrip();

    /**
     * 在分组树下挂一个 IconMode 列表（色块/渐变方/图案方），对齐 PS「组展开后网格」。
     * @return 列表指针（所有权归 tree 的 item widget）
     */
    QListWidget *attachChipGrid(QTreeWidget *tree, QTreeWidgetItem *group,
                                int iconLogical, const QSize &gridCell);

    /** 按搜索关键字显隐分组：组下无可见芯片则整组隐藏。 */
    void filterPresetTree(QTreeWidget *tree, const QString &needle);

    Ui::ColorsPanel *ui;
    bool m_syncing = false; ///< 色域 ↔ RGB 互相同步时防递归
};

#endif // COLORSPANEL_H
