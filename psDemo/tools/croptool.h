/**
 * croptool.h — 裁剪工具（tools 层）。
 *
 * 对照 GIMP Crop Tool（gimpcroptool.c → gimp_image_crop）：拖出矩形，
 * Enter / 双击确认裁切文档；Esc 取消。Shift 约束为正方形（对齐 PS）。
 */
#ifndef CROPTOOL_H
#define CROPTOOL_H

#include "tool.h"

#include <QPointF>
#include <QRectF>

namespace Ps {

class CropTool : public Tool
{
    Q_OBJECT

public:
    explicit CropTool(QObject *parent = nullptr);

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
    static QPointF constrainedEnd(const QPointF &start, const QPointF &end, bool square);
    QRectF currentImageRect() const;
    QRectF currentWidgetRect() const;
    bool commit(const ToolContext &ctx);
    void clearFrame();

    bool m_dragging = false;   ///< 正在拖出框
    bool m_hasFrame = false;   ///< 已有待确认裁剪框
    QPointF m_startImage;
    QPointF m_endImage;
    QPointF m_startWidget;
    QPointF m_endWidget;
    bool m_constrainSquare = false;
};

} // namespace Ps

#endif // CROPTOOL_H
