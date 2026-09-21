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
 * 工具选项栏（ui）。
 * 对应 Photoshop 菜单下方的上下文选项条；GIMP 中类似 tool options dock。
 * 本阶段仅显示当前工具名称，具体参数控件后续按工具再加。
 * 布局：tooloptionsbar.ui
 */
class ToolOptionsBar : public QWidget
{
    Q_OBJECT

public:
    explicit ToolOptionsBar(QWidget *parent = nullptr);
    ~ToolOptionsBar() override;

public slots:
    void setCurrentTool(Ps::ToolId id);

private:
    static QString toolDisplayName(Ps::ToolId id);

    Ui::ToolOptionsBar *ui;
};

#endif // TOOLOPTIONSBAR_H
