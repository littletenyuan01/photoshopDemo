/**
 * clonestamptool.h — 仿制图章工具（tools 层）。
 *
 * Alt+单击设源点；左键绘制。对齐 / 对所有图层取样经 ToolContext。
 * 对照 GIMP GimpCloneTool；像素经 PaintEngine::cloneStampDab。
 */
#ifndef CLONESTAMPTOOL_H
#define CLONESTAMPTOOL_H

#include "tool.h"

#include <QImage>
#include <QPointF>

namespace Ps {

class CloneStampTool : public Tool
{
    Q_OBJECT

public:
    explicit CloneStampTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;
    qreal outlineRadius(const ToolContext &ctx) const override { return ctx.brushRadius; }

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    /** 构建本笔采样图（合成或活动层贴到文档画布）。 */
    QImage buildSample(const ToolContext &ctx) const;
    /** 当前笔尖对应的采样中心（文档坐标）。 */
    QPointF sourceForDest(const QPointF &destDoc) const;

    bool m_hasSource = false;
    QPointF m_sourceDoc;           ///< Alt 设定的源点（文档）
    bool m_alignedOffsetValid = false;
    QPointF m_alignedOffset;       ///< dest − source（对齐模式跨笔保留）

    bool m_painting = false;
    QPointF m_lastImagePos;
    QPointF m_strokeOffset;        ///< 本笔 dest − source（非对齐每笔重算）
    QImage m_sample;               ///< 本笔开始时缓存的采样图
};

} // namespace Ps

#endif // CLONESTAMPTOOL_H
