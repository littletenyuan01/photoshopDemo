/**
 * pentool.h — 钢笔工具（tools 层）。
 *
 * 【对照 GIMP】app/tools/gimppathtool.c 状态机 + app/display/gimptoolpath.c 命中/绘制。
 * 【对齐 PS】点空白落点、拖出控制柄、点线段插点、Ctrl 点删点、Alt 点转换、Enter 结束。
 *
 * 【本工具不写文档】拖动中只改 m_edit；结束路径（Enter/双击）或编辑已提交路径松手时才
 * 提交并压一条撤销 —— 与 transformtool 的 preview/commit 分离同一套路。
 */
#ifndef PENTOOL_H
#define PENTOOL_H

#include "domain/path.h"
#include "tool.h"

namespace Ps {

class PenTool : public Tool
{
    Q_OBJECT
public:
    explicit PenTool(Ps::ToolId id, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool keyPress(int key, Qt::KeyboardModifiers mods,
                  const ToolContext &ctx, ViewPort &view) override;
    bool wantsShortcutOverride(int key, Qt::KeyboardModifiers mods) const override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

    /** 测例用：当前编辑中的临时路径。 */
    bool hasEdit() const { return m_hasEdit; }
    const Path &editPath() const { return m_edit; }

private:
    enum class State { Idle, DraggingOutHandle, DraggingAnchor, DraggingHandle, FreeformDrawing };
    enum class Target { None, Anchor, HandleIn, HandleOut, Segment, Empty };

    Target classify(const ToolEvent &e, const ToolContext &ctx) const;
    qreal tolDoc(const ToolContext &ctx) const;
    void ensureEditSub();
    void clearSelectionFlags();
    void selectAnchor(int sub, int idx);
    void appendAnchorAt(const QPointF &docPos);
    void finishPath(const ToolContext &ctx, bool close);
    void discardEdit();
    void commitEditToDocument(const ToolContext &ctx, const QString &label);
    void loadActiveFromDocument(const ToolContext &ctx);
    static QPointF snap45(const QPointF &origin, const QPointF &pos);

    State m_state = State::Idle;
    bool m_isCurvature = false;
    bool m_isFreeform = false;
    Path m_edit;
    bool m_hasEdit = false;
    /** 文档中正在编辑的路径下标；-1 = 尚未入库的新建会话。 */
    int m_docIndex = -1;
    int m_activeSub = -1;
    int m_activeIdx = -1;
    bool m_activeIsOut = false;
    QPointF m_dragStart;
    QPointF m_cursorDoc;
    bool m_dirtySinceCommit = false;
};

} // namespace Ps

#endif
