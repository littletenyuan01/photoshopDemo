/**
 * movetool.h — 移动工具声明（tools 层）。
 *
 * 对照 GIMP：gimpmovetool → gimp_edit_selection_tool（TRANSLATE_MODE_LAYER）。
 * 拖中真实改 offset + 投影脏区；preview_freeze 只冻缩略图，不自建 live 缓冲。
 */
#ifndef MOVETOOL_H
#define MOVETOOL_H

#include "tool.h"

#include <QPointF>

namespace Ps {

class ImageDocument;

/**
 * 移动工具（tools 层）。
 *
 * 【对照 GIMP】`gimpmovetool.c` + `gimpeditselectiontool.c`：
 * - press：undo + preview_freeze（冻缩略图）
 * - motion：translate → projection_flush（本项目 contentChanged → sync/idle）
 * - release：preview_thaw + 完整 flush
 * 不做底图+stamp（那是变换工具预览思路，不是 Move）。
 */
class MoveTool : public Tool
{
    Q_OBJECT

public:
    explicit MoveTool(QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override { return true; }
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view) override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    void finishDrag(ImageDocument *doc);

    bool m_dragging = false;
    bool m_movingMaskOnly = false; ///< 取消链接且编辑蒙版：只平移蒙版灰度
    int m_layerIndex = -1;
    QPointF m_lastImagePos;
};

} // namespace Ps

#endif // MOVETOOL_H
