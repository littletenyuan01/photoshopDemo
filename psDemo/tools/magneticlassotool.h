/**
 * magneticlassotool.h — 磁性套索选区工具（tools 层）。
 *
 * 对照 GIMP Intelligent Scissors（gimpiscissorstool + tilehandler 梯度图）与
 * PS Magnetic Lasso：拖拽时把折线吸附到局部强边缘，松手闭合写入选区。
 * 提交仍走 ImageDocument::selectPolygon → SelectPolygonOp（算子框架）。
 *
 * 本 Demo 不做全图 livewire / 种子点编辑；简化为「拖拽采样 + 邻域 Sobel 吸附」。
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
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    /** 吸附到边缘后，距末点够远才追加。 */
    void appendSnapped(const QPointF &imagePos);
    void clearStroke();

    bool m_dragging = false;
    ChannelOp m_op = ChannelOp::Replace;
    QImage m_source;             ///< 按下时合成图缓存（边缘采样用）
    QVector<QPointF> m_imagePts; ///< 文档坐标折线（已吸附）
    int m_searchRadius = 12;     ///< 边缘搜索半径（文档像素）
};

} // namespace Ps

#endif // MAGNETICLASSOTOOL_H
