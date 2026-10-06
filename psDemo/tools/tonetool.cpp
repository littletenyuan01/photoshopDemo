/**
 * tonetool.cpp — ToneTool 实现（tools 层）。
 */
#include "tonetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/paintengine.h"

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

ToneTool::ToneTool(Ps::ToolId id, ToneMode mode, QObject *parent)
    : Tool(id, parent)
    , m_mode(mode)
{
}

Qt::CursorShape ToneTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool ToneTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    Layer *layer = editableActiveLayer(ctx);
    if (!layer)
        return false;

    m_painting = true;
    m_lastImagePos = event.imagePos;

    ctx.document->pushLayerPixelsUndo(
        ctx.document->activeLayerIndex(),
        m_mode == ToneMode::Dodge ? QObject::tr("减淡") : QObject::tr("海绵"));

    const auto clip = selectionClipFor(layer, ctx.document);
    const QPointF local = layer->toLayerLocal(event.imagePos);
    const QRect dirtyLocal = PaintEngine::toneDab(
        layer->tiles(), local, ctx.brushRadius, m_mode, 0.4, 0.85, clip);
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

bool ToneTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer)
        return false;

    const auto clip = selectionClipFor(layer, ctx.document);
    const QPointF fromLocal = layer->toLayerLocal(m_lastImagePos);
    const QPointF toLocal = layer->toLayerLocal(event.imagePos);
    const QRect dirtyLocal = PaintEngine::toneStrokeSegment(
        layer->tiles(), fromLocal, toLocal, ctx.brushRadius, m_mode, 0.4, 0.85, 0.25, clip);
    m_lastImagePos = event.imagePos;
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

bool ToneTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    m_painting = false;
    return event.isLeft();
}

void ToneTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_painting = false;
}

} // namespace Ps
