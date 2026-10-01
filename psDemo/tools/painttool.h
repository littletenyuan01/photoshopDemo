/**
 * painttool.h — 画笔/橡皮/铅笔工具声明（tools 层）。
 *
 * 同一类承担 Brush / Pencil / Eraser；事件→dab 插值，像素写入 PaintEngine。
 * 铅笔 = 硬度 1.0 的硬边画笔（对照 PS Pencil / GIMP 硬笔刷）。
 */
#ifndef PAINTTOOL_H
#define PAINTTOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

/**
 * 画笔 / 铅笔 / 橡皮工具（tools 层）：在活动层像素上盖圆形 dab 并做线段插值。
 *
 * 同时承担 Brush / Pencil / Eraser 三个 ToolId（差 Mode + hardness），
 * 避免为「同一逻辑、不同参数」写多个几乎相同的类。
 * 【对照 GIMP】GIMP 是 GimpPaintTool 基类 + GimpBrushTool / GimpEraserTool 子类；
 * 本项目参数极少，合并为一个类。
 *
 * 【职责边界】本类只负责「事件 → 一串 dab 调用」；
 * 真正的像素写入在 engine/PaintEngine（对应 GIMP app/paint/GimpPaintCore）。
 */
class PaintTool : public Tool
{
    Q_OBJECT

public:
    /**
     * @param id        Brush / Pencil / Eraser
     * @param eraseMode true = 橡皮（DestinationOut），false = 画笔/铅笔（SourceOver）
     * @param hardness  dab 硬度；铅笔用 1.0，画笔/橡皮默认 0.85
     */
    PaintTool(Ps::ToolId id, bool eraseMode, qreal hardness = 0.85,
              QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;

    /** 笔尖半径为作用范围 → 画布据此画笔尖范围圈（见 Tool::outlineRadius）。 */
    qreal outlineRadius(const ToolContext &ctx) const override { return ctx.brushRadius; }

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    QString undoLabel() const;

    Ps::ToolId m_paintId;
    bool m_erase;
    qreal m_hardness = 0.85;
    bool m_painting = false;
    QPointF m_lastImagePos;
};

} // namespace Ps

#endif // PAINTTOOL_H
