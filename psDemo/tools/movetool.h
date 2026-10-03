/**
 * movetool.h — 移动工具声明（tools 层）。
 *
 * 点选、拖 offset、变换框浮层；拖中 live 预览（底图+单层），松手再投影。
 */
#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "tool.h"

#include <QImage>
#include <QPointF>
#include <QRect>

namespace Ps {

class ImageDocument;

/**
 * 移动工具（tools 层）。
 *
 * 【对照 GIMP】gimpmovetool + preview_freeze；拖中不全栈 syncProjection。
 */
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
    void startDrag(ImageDocument &doc, int layerIndex);
    void finishDrag(ImageDocument *doc);

    bool m_dragging = false;
    bool m_movingMaskOnly = false; ///< 取消链接且编辑蒙版：只平移蒙版灰度
    int m_layerIndex = -1;
    QPointF m_lastImagePos;
    QRect m_blitRect;
    QImage m_base;  ///< 跳过被拖层的底图
    QImage m_live;  ///< 底图 + 被拖层
};

} // namespace Ps

#endif // MOVETOOL_H
