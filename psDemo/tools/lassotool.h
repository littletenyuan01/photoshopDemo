/**
 * lassotool.h — 自由套索选区工具（tools 层）。
 *
 * 对照 GIMP Free Select（gimpfreeselecttool.c）：拖拽记录折线，松手闭合后
 * 经 ImageDocument::selectPolygon → PaintEngine → SelectPolygonOp 写入 mask。
 * 本 Demo 为 PS 式「按下拖拽自由手绘」；多边形套索见 polygonallassotool。
 */
#ifndef LASSOTOOL_H
#define LASSOTOOL_H

#include "domain/selection.h"
#include "tool.h"

#include <QPointF>
#include <QVector>

namespace Ps {

/**
 * 自由套索工具（ToolId::Lasso）。
 *
 * - 按下锁定 ChannelOp（Shift 加选 / Ctrl 减选 / 二者相交）
 * - 拖拽中按间距采样图像坐标点，画橡皮筋折线
 * - 松手：点数 ≥ 3 则 selectPolygon；否则放弃（不误清空）
 */
class LassoTool : public Tool
{
    Q_OBJECT

public:
    explicit LassoTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    /** 距末点超过阈值则追加（图像/控件坐标各一份）。 */
    void appendPoint(const QPointF &imagePos, const QPointF &widgetPos);

    bool m_dragging = false;
    ChannelOp m_op = ChannelOp::Replace; ///< 按下时锁定
    QVector<QPointF> m_imagePts;         ///< 文档坐标折线
    QVector<QPointF> m_widgetPts;        ///< 控件坐标折线（橡皮筋）
};

} // namespace Ps

#endif // LASSOTOOL_H
