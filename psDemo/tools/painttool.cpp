#include "painttool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"

#include <QRectF>
#include <QtMath>

namespace Ps {

namespace {

/** 由线段两端点 + 笔刷半径算出脏矩形（图像坐标，含 1px 余量防止边缘漏算）。 */
QRect dirtyRectForSegment(const QPointF &from, const QPointF &to, qreal radius)
{
    const QRectF box = QRectF(from, to).normalized();
    const int pad = qCeil(radius) + 1;
    return box.adjusted(-pad, -pad, pad, pad).toAlignedRect();
}

} // namespace

PaintTool::PaintTool(Ps::ToolId id, bool eraseMode, QObject *parent)
    : Tool(id, parent)
    , m_paintId(id)
    , m_erase(eraseMode)
{
}

QString PaintTool::displayName() const
{
    return m_erase ? tr("橡皮擦工具") : tr("画笔工具");
}

QString PaintTool::hint() const
{
    return m_erase ? tr("左键擦除活动层；Alt+左键平移")
                   : tr("左键在活动层绘制；Alt+左键平移");
}

Qt::CursorShape PaintTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool PaintTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    // 隐藏层不可绘制（与 GIMP 一致：不可见目标不接受绘制）
    if (!layer || !layer->isVisible())
        return false;

    const auto mode = m_erase ? PaintEngine::Mode::Erase : PaintEngine::Mode::Paint;

    m_painting = true;
    m_lastImagePos = event.imagePos;

    PaintEngine::stampDab(layer->pixels(), event.imagePos, ctx.brushRadius, ctx.foreground, mode);
    markDocumentDirty(dirtyRectForSegment(event.imagePos, event.imagePos, ctx.brushRadius));
    return true;
}

bool PaintTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    // 必须确认左键仍按下：否则在画布外松开时事件丢失会导致「粘笔」
    if (!(event.buttons & Qt::LeftButton))
        return false;

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer)
        return false;

    const auto mode = m_erase ? PaintEngine::Mode::Erase : PaintEngine::Mode::Paint;

    // strokeSegment 返回最后一颗 dab 的中心，作为下一段的起点，保证连续
    m_lastImagePos = PaintEngine::strokeSegment(layer->pixels(), m_lastImagePos, event.imagePos,
                                                ctx.brushRadius, ctx.foreground, mode);
    markDocumentDirty(dirtyRectForSegment(m_lastImagePos, event.imagePos, ctx.brushRadius));
    return true;
}

bool PaintTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_painting)
        return false;

    m_painting = false;
    return event.isLeft();
}

void PaintTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    // 切走工具时结束笔画，避免下次切回来续上一笔
    m_painting = false;
}

} // namespace Ps
