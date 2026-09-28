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
 * 修饰键（按下时锁定）：无修饰 Replace；Shift 加选；Ctrl 减选；Shift+Ctrl 相交。
 * 拖拽中只画橡皮筋；松手才改 mask。
 */
class MarqueeSelectTool : public Tool
{
    Q_OBJECT

public:
    enum class Shape {
        Rect,
        Ellipse,
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
    QRectF currentImageRect() const;

    Shape m_shape = Shape::Rect;
    bool m_dragging = false;
    ChannelOp m_op = ChannelOp::Replace;
    QPointF m_startImage;
    QPointF m_endImage;
    QPointF m_startWidget;
    QPointF m_endWidget;
};

} // namespace Ps

#endif // MARQUEESELECTTOOL_H
