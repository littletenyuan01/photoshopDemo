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
 * 对应 Photoshop 菜单下选项条；GIMP 类似 tool options dock。
 * 布局：tooloptionsbar.ui
 *
 * 当前：显示工具名；画笔/橡皮时显示直径 SpinBox。
 */
class ToolOptionsBar : public QWidget
{
    Q_OBJECT

public:
    explicit ToolOptionsBar(QWidget *parent = nullptr);
    ~ToolOptionsBar() override;

    int brushDiameter() const;

public slots:
    void setCurrentTool(Ps::ToolId id);
    void setBrushDiameter(int diameter);

signals:
    /** 画笔/橡皮直径变化（图像像素）。 */
    void brushDiameterChanged(int diameter);

private:
    static QString toolDisplayName(Ps::ToolId id);
    void updateForTool(Ps::ToolId id);

    Ui::ToolOptionsBar *ui;
    Ps::ToolId m_tool = Ps::ToolId::Move;
};

#endif // TOOLOPTIONSBAR_H
