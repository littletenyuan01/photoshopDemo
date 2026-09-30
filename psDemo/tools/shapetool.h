/**
 * shapetool.h — 形状工具（tools 层）。
 *
 * 拖拽画矩形/椭圆/三角形/直线到活动层；Shift 约束比例或 45°。
 * 对照 GIMP 矩形等矢量工具的精简栅格化路径。
 */
#ifndef SHAPETOOL_H
#define SHAPETOOL_H

#include "tool.h"
#include "engine/painttypes.h"

#include <QPointF>
#include <QRectF>

namespace Ps {

class ShapeTool : public Tool
{
    Q_OBJECT

public:
    ShapeTool(Ps::ToolId id, ShapeKind kind, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static QPointF constrainedEnd(const QPointF &start, const QPointF &end,
                                  ShapeKind kind, bool constrain);
    QRectF currentImageRect() const;
    QRectF currentWidgetRect() const;
    QString undoLabel() const;

    ShapeKind m_kind;
    bool m_dragging = false;
    bool m_constrain = false;
    QPointF m_startImage;
    QPointF m_endImage;
    QPointF m_startWidget;
    QPointF m_endWidget;
};

} // namespace Ps

#endif // SHAPETOOL_H
