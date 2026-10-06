/**
 * clonestamptool.cpp — CloneStampTool 实现（tools 层）。
 *
 * Alt+左键设源（CanvasView 对 CloneStamp 让出 Alt 平移）；对齐=跨笔保留偏移。
 */
#include "clonestamptool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/selection.h"
#include "engine/compositor.h"
#include "engine/paintengine.h"

#include <QPainter>
#include <QPen>
#include <QtMath>

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

CloneStampTool::CloneStampTool(QObject *parent)
    : Tool(Ps::ToolId::CloneStamp, parent)
{
}

Qt::CursorShape CloneStampTool::cursorShape() const
{
    return Qt::CrossCursor;
}

bool CloneStampTool::hasOverlay() const
{
    return m_hasSource;
}

QImage CloneStampTool::buildSample(const ToolContext &ctx) const
{
    if (!ctx.document)
        return {};

    if (ctx.cloneSampleMerged)
        return Compositor::composite(*ctx.document);

    // 活动层贴到文档画布（对照 sample-merged=false）
    QImage sample(ctx.document->width(), ctx.document->height(),
                  QImage::Format_ARGB32_Premultiplied);
    sample.fill(Qt::transparent);
    Layer *layer = ctx.document->activeLayer();
    if (layer && layer->hasPixelData()) {
        QPainter p(&sample);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(layer->offsetX(), layer->offsetY(), layer->materialize());
    }
    return sample;
}

QPointF CloneStampTool::sourceForDest(const QPointF &destDoc) const
{
    return destDoc - m_strokeOffset;
}

void CloneStampTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (!m_hasSource)
        return;

    const QPointF w = ctx.imageToWidget(m_sourceDoc);
    painter.save();
    QPen pen(Qt::white);
    pen.setCosmetic(true);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.drawLine(w + QPointF(-6, 0), w + QPointF(6, 0));
    painter.drawLine(w + QPointF(0, -6), w + QPointF(0, 6));
    pen.setColor(Qt::black);
    painter.setPen(pen);
    painter.drawLine(w + QPointF(-6, 1), w + QPointF(6, 1));
    painter.drawLine(w + QPointF(1, -6), w + QPointF(1, 6));
    painter.restore();
}

bool CloneStampTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    // Alt+单击：设源点（对照 PS Clone Stamp / GIMP Ctrl 设源；本 Demo 用 Alt）
    if (event.modifiers.testFlag(Qt::AltModifier)) {
        m_hasSource = true;
        m_sourceDoc = event.imagePos;
        m_alignedOffsetValid = false; // 重新设源后对齐偏移作废
        emit repaintRequested();
        return true;
    }

    if (!m_hasSource)
        return false;

    Layer *layer = editableActiveLayer(ctx);
    if (!layer)
        return false;

    m_sample = buildSample(ctx);
    if (m_sample.isNull())
        return false;
    if (m_sample.format() != QImage::Format_ARGB32_Premultiplied)
        m_sample = m_sample.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 对齐：跨笔保留偏移；非对齐：每笔从原源点起算
    if (ctx.cloneAlign) {
        if (!m_alignedOffsetValid) {
            m_alignedOffset = event.imagePos - m_sourceDoc;
            m_alignedOffsetValid = true;
        }
        m_strokeOffset = m_alignedOffset;
    } else {
        m_strokeOffset = event.imagePos - m_sourceDoc;
    }

    m_painting = true;
    m_lastImagePos = event.imagePos;

    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(),
                                      QObject::tr("仿制图章"));

    const auto clip = selectionClipFor(layer, ctx.document);
    const QPointF local = layer->toLayerLocal(event.imagePos);
    const QPointF src = sourceForDest(event.imagePos);
    const QRect dirtyLocal = PaintEngine::cloneStampDab(
        layer->tiles(), local, src, ctx.brushRadius, m_sample, 0.85, 1.0, clip);
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    emit repaintRequested(); // 源标记可能随对齐移动时仍固定在 m_sourceDoc
    return true;
}

bool CloneStampTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;

    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer || m_sample.isNull())
        return false;

    const auto clip = selectionClipFor(layer, ctx.document);
    const QPointF fromLocal = layer->toLayerLocal(m_lastImagePos);
    const QPointF toLocal = layer->toLayerLocal(event.imagePos);
    const QPointF srcFrom = sourceForDest(m_lastImagePos);
    const QPointF srcTo = sourceForDest(event.imagePos);

    const QRect dirtyLocal = PaintEngine::cloneStrokeSegment(
        layer->tiles(), fromLocal, toLocal, srcFrom, srcTo,
        ctx.brushRadius, m_sample, 0.85, 1.0, 0.25, clip);
    m_lastImagePos = event.imagePos;
    if (!dirtyLocal.isEmpty())
        markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

bool CloneStampTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_painting)
        return false;
    m_painting = false;
    m_sample = QImage();
    return event.isLeft();
}

void CloneStampTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_painting = false;
    m_sample = QImage();
}

} // namespace Ps
