/**
 * quickselecttool.h — 快速选择工具（tools 层，精简版）。
 *
 * PS Quick Selection 为笔刷扩张选区；GIMP 无 Fuzzy/Foreground 近似。
 * 本 Demo：拖拽中按间距对笔刷中心做连通域洪泛并以 Add 并入选区（SelectFloodOp）。
 */
#ifndef QUICKSELECTTOOL_H
#define QUICKSELECTTOOL_H

#include "domain/selection.h"
#include "tool.h"

#include <QPointF>

namespace Ps {

class QuickSelectTool : public Tool
{
    Q_OBJECT

public:
    explicit QuickSelectTool(QObject *parent = nullptr);

    QCursor cursor() const override;
    bool hasOverlay() const override { return false; }

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    static ChannelOp opFromModifiers(Qt::KeyboardModifiers modifiers);
    void sampleAt(const ToolContext &ctx, const QPointF &imagePos);

    bool m_dragging = false;
    ChannelOp m_op = ChannelOp::Add; ///< 快速选择默认加选；首击可被修饰键改成替/减/交
    QPointF m_lastSample;
};

} // namespace Ps

#endif // QUICKSELECTTOOL_H
