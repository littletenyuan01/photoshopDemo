/**
 * focustool.h — 模糊 / 锐化 / 涂抹工具（tools 层）。
 *
 * 同一类三个 ToolId；像素经 PaintEngine::focusDab。
 * 对照 GIMP GimpConvolveTool / GimpSmudgeTool。
 */
#ifndef FOCUSTOOL_H
#define FOCUSTOOL_H

#include "tool.h"
#include "engine/painttypes.h"

#include <QPointF>

namespace Ps {

class FocusTool : public Tool
{
    Q_OBJECT

public:
    FocusTool(Ps::ToolId id, FocusMode mode, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    qreal outlineRadius(const ToolContext &ctx) const override { return ctx.brushRadius; }

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    QString undoLabel() const;

    FocusMode m_mode;
    bool m_painting = false;
    QPointF m_lastImagePos;
};

} // namespace Ps

#endif // FOCUSTOOL_H
