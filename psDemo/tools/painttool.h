#ifndef PAINTTOOL_H
#define PAINTTOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

/**
 * 画笔 / 橡皮工具（tools 层）：在活动层像素上盖圆形 dab 并做线段插值。
 *
 * 同时承担 Brush 与 Eraser 两个 ToolId（只差一个 PaintEngine::Mode），
 * 避免为「同一逻辑、不同参数」写两个几乎相同的类。
 * 【对照 GIMP】GIMP 是 GimpPaintTool 基类 + GimpBrushTool / GimpEraserTool 子类，
 * 因为 GIMP 的画笔/橡皮还有各自的选项对象与 core；本项目参数极少，合并为一个类。
 *
 * 【职责边界】本类只负责「事件 → 一串 dab 调用」；
 * 真正的像素写入在 engine/PaintEngine（对应 GIMP app/paint/GimpPaintCore）。
 */
class PaintTool : public Tool
{
    Q_OBJECT

public:
    /**
     * @param id        必须是 Brush 或 Eraser
     * @param eraseMode true = 橡皮（DestinationOut），false = 画笔（SourceOver）
     */
    PaintTool(Ps::ToolId id, bool eraseMode, QObject *parent = nullptr);

    QString displayName() const override;
    QString hint() const override;
    Qt::CursorShape cursorShape() const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    /** 当前层的绘制模式。 */
    Ps::ToolId m_paintId;
    bool m_erase;
    bool m_painting = false;
    QPointF m_lastImagePos;
};

} // namespace Ps

#endif // PAINTTOOL_H
