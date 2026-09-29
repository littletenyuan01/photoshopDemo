/**
 * marqueeselecttool.h — 矩形/椭圆选框工具声明（tools 层）。
 *
 * 拖拽写入文档级 Selection；Shift/Ctrl 控制加/减/交选，拖拽中 Shift 约束 1:1。
 */
#ifndef MARQUEESELECTTOOL_H
#define MARQUEESELECTTOOL_H

#include "domain/selection.h"
#include "tool.h"

#include <QPointF>
#include <QRectF>

namespace Ps {

/**
 * 选框工具（矩形 / 椭圆），对照 GIMP Rectangle / Ellipse Select Tool。
 *
 * 拖出包围盒后写入文档级 Selection mask：
 * - Rect → gimp_channel_select_rectangle
 * - Ellipse → gimp_channel_select_ellipse（内接椭圆）
 *
 * 修饰键：
 * - 按下时锁定运算：无修饰 Replace；Shift 加选；Ctrl 减选；Shift+Ctrl 相交。
 * - 拖拽中按住 Shift：矩形→正方形、椭圆→正圆（1:1 约束，对齐 PS）。
 * 拖拽中只画橡皮筋；松手才改 mask。
 */
class MarqueeSelectTool : public Tool
{
    Q_OBJECT

public:
    /** 选框形状；构造时绑定 RectSelect 或 EllipseSelect 的 ToolId。 */
    enum class Shape {
        Rect,    ///< 矩形选框
        Ellipse, ///< 椭圆选框（内接于包围盒）
    };

    explicit MarqueeSelectTool(Shape shape, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    /** 拖拽中 Shift：把终点收到 1:1，起点不动（保留拖拽象限方向）。 */
    static QPointF constrainedEnd(const QPointF &start, const QPointF &end, bool constrain);
    QRectF currentImageRect() const;
    QRectF currentWidgetRect() const;

    Shape m_shape = Shape::Rect;          ///< 矩形或椭圆
    bool m_dragging = false;              ///< 左键拖拽进行中
    bool m_constrain = false;             ///< 拖拽中按住 Shift → 正方形/正圆
    ChannelOp m_op = ChannelOp::Replace;  ///< 按下时锁定的加/减/交/替换
    QPointF m_startImage;                 ///< 拖拽起点（图像坐标）
    QPointF m_endImage;                   ///< 拖拽终点（图像坐标，约束前）
    QPointF m_startWidget;                ///< 拖拽起点（控件坐标，画橡皮筋）
    QPointF m_endWidget;                  ///< 拖拽终点（控件坐标，约束前）
};

} // namespace Ps

#endif // MARQUEESELECTTOOL_H
