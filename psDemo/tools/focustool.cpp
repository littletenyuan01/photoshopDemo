/**
 * focustool.cpp — FocusTool 实现（tools 层）。
 */
#include "focustool.h"

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

FocusTool::FocusTool(Ps::ToolId id, FocusMode mode, QObject *parent)
    : Tool(id, parent)
    , m_mode(mode)
{
}

Qt::CursorShape FocusTool::cursorShape() const
{
    return Qt::CrossCursor;
}

QString FocusTool::undoLabel() const
{
    switch (m_mode) {
    case FocusMode::Blur: return QObject::tr("模糊");
    case FocusMode::Sharpen: return QObject::tr("锐化");
    case FocusMode::Smudge: return QObject::tr("涂抹");
    }
    return QObject::tr("聚焦");
}

bool FocusTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer || !layer->isVisible())
        return false;

    m_painting = true;
    m_lastImagePos = event.imagePos;

    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(), undoLabel());

    const auto clip = selectionClipFor(layer, ctx.document);
    const QPointF local = layer->toLayerLocal(event.imagePos);
    // 按下时涂抹无位移 → 等同轻微模糊取样自身，强度仍生效于 Blur/Sharpen
    const QRect dirtyLocal = PaintEngine::focusDab(
        layer->tiles(), local, ctx.brushRadius, m_mode, 0.5, 0.85, QPointF(), clip);
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

bool FocusTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
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
    const QRect dirtyLocal = PaintEngine::focusStrokeSegment(
        layer->tiles(), fromLocal, toLocal, ctx.brushRadius, m_mode, 0.5, 0.85, 0.25, clip);
    m_lastImagePos = event.imagePos;
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

bool FocusTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    m_painting = false;
    return event.isLeft();
}

void FocusTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_painting = false;
}

} // namespace Ps
