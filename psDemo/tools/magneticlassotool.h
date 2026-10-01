/**
 * magneticlassotool.h — 磁性套索（tools 层）。
 *
 * 流程：单击落首锚 → 移动圆域吸边 → 锚点到 tip 走 Livewire → Frequency 自动落锚。
 */
#ifndef MAGNETICLASSOTOOL_H
#define MAGNETICLASSOTOOL_H

#include "domain/selection.h"
#include "tool.h"

#include <QImage>
#include <QPointF>
#include <QVector>

namespace Ps {

class MagneticLassoTool : public Tool
{
    Q_OBJECT

public:
    explicit MagneticLassoTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool keyPress(int key, Qt::KeyboardModifiers modifiers,
                  const ToolContext &ctx, ViewPort &view) override;
    bool wantsShortcutOverride(int key, Qt::KeyboardModifiers modifiers) const override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    void syncParamsFromContext(const ToolContext &ctx);
    bool ensureSource(const ToolContext &ctx);
    void trackAlongEdge(const QPointF &imagePos);
    void addFastener(const QPointF &gridPos);
    void rebuildLiveSegment(const QPointF &tip);
    void undoLastFastener();
    bool nearFirstWidget(const QPointF &widgetPos, const ToolContext &ctx) const;
    bool commit(const ToolContext &ctx);
    void clearStroke();

    bool m_active = false;
    ChannelOp m_op = ChannelOp::Replace;
    QImage m_source;
    bool m_sourceReady = false;

    QVector<QPointF> m_fasteners; ///< 已落锚点（整数格点）
    QVector<QPointF> m_edgePath;  ///< 已提交段 + 实时贴合段
    QPointF m_tip;
    bool m_hasTip = false;

    QPointF m_cursorImage;
    bool m_hasCursor = false;

    int m_searchRadius = 10;           ///< Width：圆半径
    qreal m_minEdge = 48.0;            ///< Contrast
    qreal m_anchorSpacing = 19.0;      ///< Frequency → 锚点最小间距（文档像素）
    qreal m_travelSinceFastener = 0.0; ///< 自上一锚点起、光标累计行进距离
    QPointF m_prevCursor;              ///< 上一次鼠标位置（算行进距离用）
    bool m_hasPrevCursor = false;
};

} // namespace Ps

#endif // MAGNETICLASSOTOOL_H
