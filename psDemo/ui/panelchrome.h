#ifndef PANELCHROME_H
#define PANELCHROME_H

#include <QTabWidget>
#include <QToolButton>
#include <QtGlobal>

/**
 * 停靠面板的公共外观件（ui）。
 *
 * 【为什么单独一个头】三个面板（LayerPanel / ColorsPanel / PropertiesPanel）都要在
 * Tab 栏右上角挂同一个 PS 式 ≡ 菜单按钮。同一段代码抄三遍，改一处漏两处，
 * 故集中到这里；样式（QToolButton#btnPanelMenu）在 resources/styles/dark.qss。
 *
 * 【对照 GIMP】GIMP 每个 dockable 自带 GimpDockbook 菜单（gimp_dockbook.c），
 * 本项目按 PS 外观统一成一个 ≡ 入口。
 */
namespace PanelChrome {

/**
 * 在 tabs 的右上角创建 ≡ 菜单按钮；返回该按钮（所有权归 tabs）。
 * 【不变量】tabs 必须非空 —— 调用点一律传 .ui 成员，永远有效；
 * 因此这里不做「为空就静默跳过」的兜底，避免按钮悄悄消失这种隐性 bug。
 */
inline QToolButton *addMenuButton(QTabWidget *tabs)
{
    Q_ASSERT(tabs);
    auto *button = new QToolButton(tabs);
    button->setObjectName(QStringLiteral("btnPanelMenu"));
    button->setText(QStringLiteral("≡"));
    button->setToolTip(QStringLiteral("面板选项"));
    button->setAutoRaise(true);
    tabs->setCornerWidget(button, Qt::TopRightCorner);
    return button;
}

} // namespace PanelChrome

#endif // PANELCHROME_H
