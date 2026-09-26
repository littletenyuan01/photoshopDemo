#ifndef TOOLOPTIONSBAR_H
#define TOOLOPTIONSBAR_H

#include "tools/toolid.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ToolOptionsBar;
}
QT_END_NAMESPACE

/**
 * 工具选项栏（ui）—— Photoshop 的「选项条」/ GIMP 的 tool options。
 *
 * 【对照 GIMP】GIMP 的做法是：`GimpToolOptions` 基类 + 每个工具（族）实现
 * `gimp_tool_options_gui()` 虚函数，由 `gimptooloptions-gui.c` 里的 `gimp_prop_*`
 * 构造器按属性生成控件；工具切换时整块 GUI 替换，由
 * `gimp-tool-options-manager.c` 的 `gimp_tools_get_tool_options_gui()` 按需创建。
 *
 * 本项目取同样的形状，但用 Qt 的方式落地：**一个 `.ui` 内放 `QStackedWidget`，
 * 每个工具族一页**，切换工具即 `setCurrentWidget()`。这样既对齐 GIMP 的
 * 「按工具族整块切换」，又满足 `qt-ui-forms.mdc`（界面必须落在 `.ui` 里，
 * 便于 Designer 调整；纯代码堆控件是该规则明确列出的反例）。
 *
 * 参数名对照 GIMP 的真实属性（`operation` / `feather-radius` / `antialias` /
 * `paint-mode` / `opacity` / `clone-type` / `sample-merged` / `align-mode` /
 * `gradient-type` / `gradient-repeat` / `path-polygonal` 等），见各控件 toolTip。
 *
 * ⚠️ **现状**：只有「大小」（`brushSizeSpin`）真正接到绘制（画笔/橡皮直径）。
 * 其余所有控件都是 **UI 占位**，不改变任何行为；占位工具的提示语明确写了
 * 「逻辑尚未接入」。这是刻意的：界面完整可演示，但不假装有功能。
 */
class ToolOptionsBar : public QWidget
{
    Q_OBJECT

public:
    explicit ToolOptionsBar(QWidget *parent = nullptr);
    ~ToolOptionsBar() override;

    int brushDiameter() const;

    /**
     * 当前工具的提示语（由 MainWindow 显示在状态栏）。
     * 【为什么不在选项条里】PS 的选项条只有参数，没有说明文字；放在条里既浪费宽度，
     * 又会被 Expanding 的选项区挤到最右边、离参数很远。
     */
    QString currentHint() const { return m_hint; }

public slots:
    void setCurrentTool(Ps::ToolId id);
    void setBrushDiameter(int diameter);

signals:
    /** 画笔/橡皮直径变化（图像像素）。唯一真正接线的选项。 */
    void brushDiameterChanged(int diameter);

private:
    static QString toolDisplayName(Ps::ToolId id);

    /** 工具 → 选项页。对应 GIMP 每个工具族自己的 gimp_tool_options_gui()。 */
    QWidget *pageForTool(Ps::ToolId id) const;

    /** 同一页内「部分工具才有的控件」显隐：选区页的魔棒项、绘画页的图章项。 */
    void updateToolSpecificControls(Ps::ToolId id);

    /** 选项栏右侧的提示语：已接入的写用法，占位的写明尚未接入。 */
    static QString hintForTool(Ps::ToolId id);

    Ui::ToolOptionsBar *ui;
    Ps::ToolId m_tool = Ps::ToolId::Move;
    QString m_hint;   ///< 当前工具提示语（状态栏用，见 currentHint()）
};

#endif // TOOLOPTIONSBAR_H
