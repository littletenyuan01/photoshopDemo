/**
 * tonetool.h — 减淡 / 海绵工具（tools 层）。
 *
 * 对照 GIMP DodgeBurnTool / SpongeTool；像素经 PaintEngine::toneDab。
 */
#ifndef TONETOOL_H
#define TONETOOL_H

#include "tool.h"
#include "engine/painttypes.h"

#include <QPointF>

namespace Ps {

class ToneTool : public Tool
{
    Q_OBJECT

public:
    ToneTool(Ps::ToolId id, ToneMode mode, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    qreal outlineRadius(const ToolContext &ctx) const override { return ctx.brushRadius; }

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    ToneMode m_mode;
    bool m_painting = false;
    QPointF m_lastImagePos;
};

} // namespace Ps

#endif // TONETOOL_H
