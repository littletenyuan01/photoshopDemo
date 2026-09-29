#include "painttool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/paintengine.h"

#include <QRectF>
#include <QtMath>

namespace Ps {

namespace {

/** 由线段两端点 + 笔刷半径算出脏矩形（**文档**坐标，含 1px 余量）。 */
QRect dirtyRectForSegment(const QPointF &fromDoc, const QPointF &toDoc, qreal radius)
{
    const QRectF box = QRectF(fromDoc, toDoc).normalized();
    const int pad = qCeil(radius) + 1;
    return box.adjusted(-pad, -pad, pad, pad).toAlignedRect();
}

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
    PaintEngine::stampDab(layer->tiles(), local, ctx.brushRadius, ctx.foreground, mode, 0.85, clip);
    markDocumentDirty(ctx, dirtyRectForSegment(event.imagePos, event.imagePos, ctx.brushRadius));
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
    // strokeSegment 返回层内最后 dab；再映回文档坐标作下一段起点
    const QPointF lastLocal = PaintEngine::strokeSegment(
        layer->tiles(), fromLocal, toLocal,
        ctx.brushRadius, ctx.foreground, mode, 0.85, 0.25, clip);
    m_lastImagePos = lastLocal + QPointF(layer->offsetX(), layer->offsetY());
    markDocumentDirty(ctx, dirtyRectForSegment(m_lastImagePos, event.imagePos, ctx.brushRadius));
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
