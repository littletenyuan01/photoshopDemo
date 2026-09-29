/**
 * polygonallassotool.h — 多边形套索选区工具（tools 层）。
 *
 * 对照 GIMP Polygon Select（gimppolygonselecttool.c）+ Free Select 的折线段：
 * 单击落点，橡皮筋跟鼠标；闭合后经 selectPolygon → SelectPolygonOp。
 * 对齐 PS：双击 / Enter 闭合；点近起点闭合；Esc 取消；Backspace 撤末点；
 * 拖中按住 Shift → 吸附水平 / 垂直 / 前一边方向及其垂线（含 45° 轴对齐）。
 */
#ifndef POLYGONALLASSOTOOL_H
#define POLYGONALLASSOTOOL_H

#include "domain/selection.h"
#include "tool.h"

#include <QPointF>
#include <QVector>

namespace Ps {

/**
 * 多边形套索（ToolId::PolygonalLasso）。
 *
 * 与自由套索共用 SelectPolygonOp；交互为「点选顶点」而非拖拽采样。
 */
class PolygonalLassoTool : public Tool
{
    Q_OBJECT

public:
    explicit PolygonalLassoTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool keyPress(int key, Qt::KeyboardModifiers modifiers,
                  const ToolContext &ctx, ViewPort &view) override;
    bool wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    /**
     * Shift 约束：把 cursor 投影到距 origin 等长、角度落在候选方向上的点。
     * 候选 = 轴对齐 45° 步进（PS）+ 相对前一边的平行/垂直（若有 prevOrigin）。
     */
    static QPointF constrainEnd(const QPointF &origin, const QPointF &cursor,
                                const QPointF *prevOrigin, bool shift);
    /** 按当前修饰键更新橡皮筋终点（图像 + 控件坐标）。 */
    void updateCursor(const QPointF &imagePos, const QPointF &widgetPos, bool shift);
    /** 控件坐标是否落在起点闭合热区内。 */
    bool nearFirstWidget(const QPointF &widgetPos) const;
    void appendVertex(const QPointF &imagePos, const QPointF &widgetPos);
    void clearPath();
    /** 点数 ≥ 3 则写入选区并清路径；否则仅返回 false。 */
    bool commit(const ToolContext &ctx);
    /** 橡皮筋终点（已含 Shift 吸附与起点热区）。 */
    QPointF rubberWidgetEnd() const;

    bool m_active = false;               ///< 正在编辑多边形
    ChannelOp m_op = ChannelOp::Replace; ///< 首点按下时锁定
    QVector<QPointF> m_imagePts;         ///< 已确认顶点（文档坐标）
    QVector<QPointF> m_widgetPts;        ///< 已确认顶点（控件坐标）
    QPointF m_cursorImage;               ///< 橡皮筋终点（文档，可已吸附）
    QPointF m_cursorWidget;              ///< 橡皮筋终点（控件，可已吸附）
};

} // namespace Ps

#endif // POLYGONALLASSOTOOL_H
