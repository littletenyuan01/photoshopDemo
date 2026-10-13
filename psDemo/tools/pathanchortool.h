/**
 * pathanchortool.h — 添加 / 删除 / 转换锚点工具（tools 层）。
 *
 * 三个 ToolId 共用本类；几何一律走 PathEditOps，本类只做命中 + 压撤销。
 */
#ifndef PATHANCHORTOOL_H
#define PATHANCHORTOOL_H

#include "domain/path.h"
#include "tool.h"

namespace Ps {

class PathAnchorTool : public Tool
{
    Q_OBJECT
public:
    explicit PathAnchorTool(Ps::ToolId id, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;
    bool hasOverlay() const override;
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;

private:
    enum class Mode { Add, Delete, Convert };
    Mode m_mode = Mode::Add;

    qreal tolDoc(const ToolContext &ctx) const;
    bool loadActive(const ToolContext &ctx);
    void commit(const ToolContext &ctx, const QString &label);

    Path m_path;
    int m_docIndex = -1;
    bool m_hasPath = false;
};

} // namespace Ps

#endif
