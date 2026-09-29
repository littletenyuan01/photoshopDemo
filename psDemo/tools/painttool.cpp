#include "painttool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/paintengine.h"

#include <QPointF>

namespace Ps {

namespace {

PaintEngine::SelectionClip selectionClipFor(Layer *layer, ImageDocument *doc)
{
    PaintEngine::SelectionClip clip;
    if (!layer || !doc)
        return clip;
    clip.selection = &doc->selection();
    clip.layerOffsetX = layer->offsetX();
    clip.layerOffsetY = layer->offsetY();
    return clip;
}

} // namespace

PaintTool::PaintTool(Ps::ToolId id, bool eraseMode, QObject *parent)
    : Tool(id, parent)
    , m_paintId(id)
    , m_erase(eraseMode)
{
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
    const auto clip = selectionClipFor(layer, ctx.document);

    m_painting = true;
    m_lastImagePos = event.imagePos;

    // 对照 gimp_drawable_push_undo：改像素前推入旧缓冲；一笔一条
    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(),
                                      m_erase ? QObject::tr("橡皮擦") : QObject::tr("画笔"));

    // 瓦片是层内坐标：文档点先减 Layer offset（对照 drawable 局部坐标）
    const QPointF local = layer->toLayerLocal(event.imagePos);
    // 脏区由算子返回（已与层范围求交），工具侧不再自己推算
    const QRect dirtyLocal = PaintEngine::stampDab(
        layer->tiles(), local, ctx.brushRadius, ctx.foreground, mode, 0.85, clip);
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
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
    const auto clip = selectionClipFor(layer, ctx.document);

    const QPointF fromLocal = layer->toLayerLocal(m_lastImagePos);
    const QPointF toLocal = layer->toLayerLocal(event.imagePos);
    // 一次鼠标移动 = 一段笔画；算子返回本段扫过的**全部** dab 的并集脏区。
    // （旧代码用「已更新过的」m_lastImagePos 当脏区起点，等于只标了末端一个 dab）
    const QRect dirtyLocal = PaintEngine::strokeSegment(
        layer->tiles(), fromLocal, toLocal,
        ctx.brushRadius, ctx.foreground, mode, 0.85, 0.25, clip);
    m_lastImagePos = event.imagePos;
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
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
