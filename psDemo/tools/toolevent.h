#ifndef TOOLEVENT_H
#define TOOLEVENT_H

#include <QPointF>
#include <Qt>

namespace Ps {

/**
 * 规范化后的工具事件（tools 层）。
 *
 * 【关键设计】CanvasView 在分发前**已把控件坐标换算成图像坐标**，
 * 工具因此完全不需要知道当前缩放/偏移，也不需要 QWidget 的任何类型。
 * 这对应 GIMP 中 GimpTool 直接拿到 image 坐标的约定。
 *
 * 新增工具时只处理本结构，**不需要修改 CanvasView**。
 */
struct ToolEvent
{
    /// 图像坐标（可能落在画布外，工具自行判断）
    QPointF imagePos;
    /// 控件坐标（仅少数需要锚定视口的工具用，如缩放）
    QPointF widgetPos;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    bool isLeft() const { return button == Qt::LeftButton; }
    bool isRight() const { return button == Qt::RightButton; }
    bool isMiddle() const { return button == Qt::MiddleButton; }

    /// Alt+左键 = 临时平移（对齐 PS/GIMP 的通用手势）
    bool isAltLeft() const
    {
        return button == Qt::LeftButton && modifiers.testFlag(Qt::AltModifier);
    }
};

} // namespace Ps

#endif // TOOLEVENT_H
