/**
 * movetool.h — 移动工具声明（tools 层）。
 *
 * 拖中：below/above 缓存 + 层图章贴图（跟手）；松手：live 写入投影，位置立刻固定。
 */
#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "tool.h"

#include <QImage>
#include <QPointF>
#include <QRect>

namespace Ps {

class ImageDocument;

class MoveTool : public Tool
{
    Q_OBJECT

public:
    explicit MoveTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override { return true; }
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;
    const QImage *liveProjection() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    void finishDrag(ImageDocument *doc);
    void clearLive();
    bool buildLiveStacks(ImageDocument *doc, int layerIndex,
                         const QImage *projectionSnapshot);
    void rebuildLive(ImageDocument *doc, const QRect &patchDoc = QRect());
    QRect stampDocRect(const Layer *layer, const QRect &docBounds) const;
    /** 上方是否有启用的调整层（决定不能烘焙 above / 不能复用投影快照）。 */
    static bool hasAdjustmentAbove(const ImageDocument *doc, int layerIndex);

    bool m_dragging = false;
    bool m_movingMaskOnly = false;
    bool m_useLive = false;
    bool m_liveApplyAbove = false; ///< true：每帧对补丁 blend 上方栈（含调整层）
    int m_layerIndex = -1;
    QPointF m_lastImagePos;
    QRect m_liveLayerBounds;
    QImage m_below;
    QImage m_above;     ///< 仅无调整层上方时烘焙；否则空，走 liveApplyAbove
    QImage m_stamp;     ///< 按下时 composite raster（层局部，含滤镜/样式）
    int m_stampDrawDx = 0;
    int m_stampDrawDy = 0;
    qreal m_stampOpacity = 1.0;
    QImage m_live;
};

} // namespace Ps

#endif // MOVETOOL_H
