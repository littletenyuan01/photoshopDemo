#ifndef COLORSPANEL_H
#define COLORSPANEL_H

#include <QColor>
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
 * 【外观对照】Adobe Photoshop 右侧「颜色」停靠组四页 Tab：
 * - 颜色：重叠前景/背景方块 + 二维 S/V 色域 + 竖直色相条（`HsvColorWell`）
 * - 色板 / 渐变 / 图案：搜索 + 分组网格（占位数据）
 *
 * 前景/背景经信号与工具箱、画布 ToolContext 同步。
 */
class ColorsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorsPanel(QWidget *parent = nullptr);
    ~ColorsPanel() override;

    QColor foregroundColor() const;
    QColor backgroundColor() const;

public slots:
    void setForegroundColor(const QColor &color);
    void setBackgroundColor(const QColor &color);

signals:
    void foregroundColorChanged(const QColor &color);
    void backgroundColorChanged(const QColor &color);

private slots:
    void onRgbChanged();
    void onHexEdited();
    void onWellColorChanged(const QColor &color);
    void onWellSwatchesSwapped();
    void onSwatchSearchChanged(const QString &text);
    void onGradientSearchChanged(const QString &text);
    void onPatternSearchChanged(const QString &text);

private:
    void buildSwatchGroups();
    void buildGradientGroups();
    void buildPatternGroups();
    void syncRecentStrip();

    QListWidget *attachChipGrid(QTreeWidget *tree, QTreeWidgetItem *group,
                                QListWidget *appearanceTemplate);
    void filterPresetTree(QTreeWidget *tree, const QString &needle);
    /** 按模板格子尺寸与条目数估算网格高度（内容量相关，须留在运行时）。 */
    void fitChipGridHeight(QListWidget *grid, int itemCount, int colsHint);

    Ui::ColorsPanel *ui;
    bool m_syncing = false;  ///< 色域 ↔ RGB 互相同步时防递归
    bool m_emitting = false; ///< 对外同步回写时防回环
};

#endif // COLORSPANEL_H
