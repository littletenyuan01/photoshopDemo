/**
 * painttool.cpp — 画笔/铅笔/橡皮工具实现（tools 层）。
 */
#include "painttool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/selection.h"
#include "engine/paintengine.h"

#include <QPointF>

namespace Ps {

namespace {

/** 组装选区裁剪参数：文档选区 + 活动层 offset（层内坐标绘制用）。 */
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

/** 前景色亮度 → 蒙版灰度（黑藏白显）。 */
quint8 maskGrayFromColor(const QColor &c)
{
    return quint8(qBound(0, qGray(c.rgba()), 255));
}

} // namespace

PaintTool::PaintTool(Ps::ToolId id, bool eraseMode, qreal hardness, QObject *parent)
    : Tool(id, parent)
    , m_paintId(id)
    , m_erase(eraseMode)
    , m_hardness(qBound(0.0, hardness, 1.0))
{
}

QString PaintTool::undoLabel() const
{
    if (m_erase)
        return QObject::tr("橡皮擦");
    if (m_paintId == Ps::ToolId::Pencil)
        return QObject::tr("铅笔");
    return QObject::tr("画笔");
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
    const QPointF local = layer->toLayerLocal(event.imagePos);

    m_painting = true;
    m_lastImagePos = event.imagePos;

    // 蒙版编辑：写灰度；撤销走属性快照（含 maskGray）
    if (ctx.document->isEditingLayerMask()) {
        LayerMask *mask = layer->mask();
        if (!mask || mask->isNull())
            return false;
        ctx.document->pushLayerPropertiesUndo(ctx.document->activeLayerIndex(),
                                              undoLabel() + QObject::tr("（蒙版）"));
        const QRect dirtyLocal = PaintEngine::stampMaskDab(
            mask->image(), local, ctx.brushRadius,
            maskGrayFromColor(ctx.foreground), mode, m_hardness, clip);
        if (!dirtyLocal.isEmpty())
            markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
        return true;
    }

    // 对照 gimp_drawable_push_undo：改像素前推入旧缓冲；一笔一条
    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(), undoLabel());

    // 瓦片是层内坐标：文档点先减 Layer offset（对照 drawable 局部坐标）
    const QRect dirtyLocal = PaintEngine::stampDab(
        layer->tiles(), local, ctx.brushRadius, ctx.foreground, mode, m_hardness, clip);
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
    m_lastImagePos = event.imagePos;

    if (ctx.document->isEditingLayerMask()) {
        LayerMask *mask = layer->mask();
        if (!mask || mask->isNull())
            return false;
        const QRect dirtyLocal = PaintEngine::strokeMaskSegment(
            mask->image(), fromLocal, toLocal,
            ctx.brushRadius, maskGrayFromColor(ctx.foreground), mode, m_hardness, 0.25, clip);
        if (!dirtyLocal.isEmpty())
            markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
        return true;
    }

    // 一次鼠标移动 = 一段笔画；算子返回本段扫过的**全部** dab 的并集脏区。
    const QRect dirtyLocal = PaintEngine::strokeSegment(
        layer->tiles(), fromLocal, toLocal,
        ctx.brushRadius, ctx.foreground, mode, m_hardness, 0.25, clip);
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
    m_painting = false;
}

} // namespace Ps
