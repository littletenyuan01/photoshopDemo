/**
 * movetool.cpp — 移动工具实现（tools 层）。
 *
 * 跟手预览：press 建 below + 层图章；motion 只在脏区重贴。
 * 上方有调整层时：below 不用投影快照，每帧对补丁重跑上方栈（挖空区也吃调整）。
 * 松手：liveProjectionCommitted → 投影 adopt，画面不闪、位置立刻固定。
 */
#include "movetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/blendmode.h"
#include "engine/compositor.h"

#include <QPainter>
#include <QPen>
#include <QtMath>

namespace Ps {

namespace {

const QColor kTransformBlue(26, 159, 255);
constexpr qreal kHandleSize = 7.0;

void applyMaskToStamp(QImage &stamp, const LayerMask *mask)
{
    if (stamp.isNull() || !mask || mask->isNull())
        return;
    for (int y = 0; y < stamp.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(stamp.scanLine(y));
        for (int x = 0; x < stamp.width(); ++x) {
            const int ma = mask->valueAt(x, y);
            if (ma == 255)
                continue;
            if (ma == 0) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            const QRgb px = line[x];
            const int a = qAlpha(px) * ma / 255;
            line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), a);
        }
    }
}

} // namespace

MoveTool::MoveTool(QObject *parent)
    : Tool(Ps::ToolId::Move, parent)
{
}

Qt::CursorShape MoveTool::cursorShape() const
{
    return Qt::SizeAllCursor;
}

const QImage *MoveTool::liveProjection() const
{
    if (m_useLive && !m_live.isNull())
        return &m_live;
    return nullptr;
}

void MoveTool::clearLive()
{
    m_useLive = false;
    m_liveApplyAbove = false;
    m_liveLayerBounds = {};
    m_below = QImage();
    m_above = QImage();
    m_stamp = QImage();
    m_stampDrawDx = 0;
    m_stampDrawDy = 0;
    m_live = QImage();
    m_stampOpacity = 1.0;
}

QRect MoveTool::stampDocRect(const Layer *layer, const QRect &docBounds) const
{
    if (!layer || m_stamp.isNull())
        return {};
    return QRect(layer->offsetX() + m_stampDrawDx,
                 layer->offsetY() + m_stampDrawDy,
                 m_stamp.width(), m_stamp.height())
        .intersected(docBounds);
}

bool MoveTool::hasAdjustmentAbove(const ImageDocument *doc, int layerIndex)
{
    if (!doc)
        return false;
    const int n = doc->layers().count();
    for (int i = layerIndex + 1; i < n; ++i) {
        const Layer *L = doc->layers().layerAt(i);
        if (L && L->isVisible() && L->isAdjustmentLayer() && L->filters().hasEnabled())
            return true;
    }
    return false;
}

bool MoveTool::buildLiveStacks(ImageDocument *doc, int layerIndex,
                               const QImage *projectionSnapshot)
{
    if (!doc || layerIndex < 0 || layerIndex >= doc->layers().count())
        return false;
    Layer *layer = doc->layers().layerAt(layerIndex);
    if (!layer || layer->isAdjustmentLayer() || !layer->hasPixelData())
        return false;

    const int w = doc->width();
    const int h = doc->height();
    if (w <= 0 || h <= 0)
        return false;

    const QRect full(0, 0, w, h);
    const QRect layerBounds = layer->styleBoundsInDocument().intersected(full);
    m_liveApplyAbove = hasAdjustmentAbove(doc, layerIndex);

    // 上方有调整层时：不能复用「已含调整结果」的投影快照，否则挖空区不会重跑调整；
    // below 必须是纯下方栈；above 每帧对真实内容 blend（不能烘焙到透明底）。
    if (!m_liveApplyAbove && projectionSnapshot && !projectionSnapshot->isNull()
        && projectionSnapshot->width() == w && projectionSnapshot->height() == h) {
        m_below = projectionSnapshot->copy();
        if (!layerBounds.isEmpty())
            Compositor::blendLayerRange(m_below, *doc, layerBounds, 0, layerIndex, -1);
    } else {
        m_below = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_below.fill(Qt::transparent);
        if (!Compositor::blendLayerRange(m_below, *doc, full, 0, layerIndex, -1))
            return false;
    }

    if (m_liveApplyAbove) {
        m_above = QImage();
    } else {
        m_above = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        m_above.fill(Qt::transparent);
        if (!Compositor::blendLayerRange(m_above, *doc, full, layerIndex + 1,
                                         doc->layers().count(), -1))
            return false;
    }

    const Layer::CompositeRaster raster = layer->ensureCompositeRaster();
    if (raster.image.isNull())
        return false;
    m_stamp = raster.image;
    if (m_stamp.format() != QImage::Format_ARGB32_Premultiplied)
        m_stamp = m_stamp.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_stampDrawDx = raster.originDx;
    m_stampDrawDy = raster.originDy;
    m_stampOpacity = layer->opacity();

    // 图章快路径：Normal + 可预烘焙蒙版；其它混合模式走局部 blendLayerRange
    if (layer->blendMode() != BlendMode::Normal) {
        m_stamp = QImage();
    } else if (layer->hasMask() && layer->mask() && layer->mask()->isEnabled()) {
        applyMaskToStamp(m_stamp, layer->mask());
    }

    return true;
}

void MoveTool::rebuildLive(ImageDocument *doc, const QRect &patchDoc)
{
    if (!doc || m_below.isNull() || m_layerIndex < 0)
        return;

    const QRect full(0, 0, doc->width(), doc->height());
    Layer *layer = doc->layers().layerAt(m_layerIndex);
    if (!layer)
        return;

    const QPoint stampPos(layer->offsetX() + m_stampDrawDx,
                          layer->offsetY() + m_stampDrawDy);
    const int layerCount = doc->layers().count();

    auto paintPatch = [&](const QRect &areaIn) {
        const QRect area = areaIn.intersected(full);
        if (area.isEmpty())
            return;
        {
            QPainter p(&m_live);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(area.topLeft(), m_below, area);
        }
        if (!m_stamp.isNull()) {
            QPainter p(&m_live);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setOpacity(m_stampOpacity);
            p.drawImage(stampPos, m_stamp);
            p.setOpacity(1.0);
        } else {
            Compositor::blendLayerRange(m_live, *doc, area, m_layerIndex,
                                        m_layerIndex + 1, -1);
        }
        if (m_liveApplyAbove) {
            // 对 below+图章 的真实像素重跑上方栈（调整层才能吃到挖空区）
            Compositor::blendLayerRange(m_live, *doc, area, m_layerIndex + 1,
                                        layerCount, -1);
        } else if (!m_above.isNull()) {
            QPainter p(&m_live);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.drawImage(area.topLeft(), m_above, area);
        }
    };

    if (m_live.isNull() || patchDoc.isEmpty()) {
        m_live = m_below.copy();
        if (!m_stamp.isNull()) {
            QPainter p(&m_live);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setOpacity(m_stampOpacity);
            p.drawImage(stampPos, m_stamp);
        } else {
            Compositor::blendLayerRange(m_live, *doc, full, m_layerIndex,
                                        m_layerIndex + 1, -1);
        }
        if (m_liveApplyAbove) {
            Compositor::blendLayerRange(m_live, *doc, full, m_layerIndex + 1,
                                        layerCount, -1);
        } else if (!m_above.isNull()) {
            QPainter p(&m_live);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.drawImage(0, 0, m_above);
        }
    } else {
        paintPatch(patchDoc);
    }

    m_liveLayerBounds = layer->styleBoundsInDocument().intersected(full);
    m_useLive = true;
}

void MoveTool::finishDrag(ImageDocument *doc)
{
    if (doc && doc->isPreviewFrozen())
        doc->endPreviewFreeze();

    // adopt 后再清 live：CanvasView 同步处理信号，同一帧内投影已更新
    if (m_useLive && !m_live.isNull()) {
        emit liveProjectionCommitted(m_live);
        if (doc)
            doc->clearDirtyRect();
    } else if (doc) {
        const QRect dirty = doc->dirtyRect();
        if (dirty.isEmpty())
            doc->markDirty();
        else
            doc->markDirty(dirty);
    }

    clearLive();
    m_layerIndex = -1;
}

void MoveTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    if (!ctx.document)
        return;
    const Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return;

    const QRect content = layer->contentBoundsInDocument();
    if (content.isEmpty())
        return;

    const QPointF tl = ctx.imageToWidget(QPointF(content.left(), content.top()));
    const QPointF br = ctx.imageToWidget(QPointF(content.right() + 1, content.bottom() + 1));
    QRectF box = QRectF(tl, br).normalized();
    if (box.width() < 0.5 && box.height() < 0.5)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen boxPen(kTransformBlue);
    boxPen.setCosmetic(true);
    boxPen.setWidth(1);
    painter.setPen(boxPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));

    const qreal half = kHandleSize * 0.5;
    const QPointF centers[8] = {
        box.topLeft(),
        {box.center().x(), box.top()},
        box.topRight(),
        {box.right(), box.center().y()},
        box.bottomRight(),
        {box.center().x(), box.bottom()},
        box.bottomLeft(),
        {box.left(), box.center().y()},
    };
    painter.setBrush(Qt::white);
    for (const QPointF &c : centers)
        painter.drawRect(QRectF(c.x() - half, c.y() - half, kHandleSize, kHandleSize));
    painter.restore();
}

bool MoveTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft() || !ctx.document)
        return false;

    const int picked = ctx.document->pickLayerAt(qFloor(event.imagePos.x()),
                                                 qFloor(event.imagePos.y()));
    if (picked >= 0)
        ctx.document->setActiveLayerIndex(picked);

    Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return false;

    const int index = ctx.document->activeLayerIndex();
    m_dragging = true;
    m_movingMaskOnly = false;
    m_layerIndex = index;
    m_lastImagePos = event.imagePos;
    clearLive();

    ctx.document->beginPreviewFreeze();

    if (ctx.document->isEditingLayerMask()
        && layer->mask() && !layer->mask()->isNull()
        && !layer->mask()->isLinked()) {
        m_movingMaskOnly = true;
        ctx.document->pushLayerPropertiesUndo(index, QObject::tr("移动蒙版"));
        emit repaintRequested();
        return true;
    }

    ctx.document->pushLayerOffsetUndo(index);

    if (buildLiveStacks(ctx.document, index, ctx.projectionSnapshot))
        rebuildLive(ctx.document);

    emit repaintRequested();
    return true;
}

bool MoveTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging || !(event.buttons & Qt::LeftButton) || !ctx.document)
        return false;
    if (m_layerIndex < 0)
        return true;

    const int dx = qRound(event.imagePos.x() - m_lastImagePos.x());
    const int dy = qRound(event.imagePos.y() - m_lastImagePos.y());
    if (dx == 0 && dy == 0)
        return true;

    if (m_movingMaskOnly) {
        ctx.document->shiftLayerMask(m_layerIndex, dx, dy);
    } else {
        const QRect docRect(0, 0, ctx.document->width(), ctx.document->height());
        Layer *layer = ctx.document->layers().layerAt(m_layerIndex);
        const QRect oldStamp = stampDocRect(layer, docRect);
        const QRect oldBounds = layer
            ? layer->styleBoundsInDocument().intersected(docRect)
            : m_liveLayerBounds;
        ctx.document->translateLayer(m_layerIndex, dx, dy, /*emitContent=*/!m_useLive);
        if (m_useLive && layer) {
            const QRect newStamp = stampDocRect(layer, docRect);
            const QRect newBounds = layer->styleBoundsInDocument().intersected(docRect);
            QRect patch = oldBounds.united(newBounds).united(oldStamp).united(newStamp);
            rebuildLive(ctx.document, patch.intersected(docRect));
        }
    }

    m_lastImagePos += QPointF(dx, dy);
    emit repaintRequested();
    return true;
}

bool MoveTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    m_dragging = false;
    m_movingMaskOnly = false;
    finishDrag(ctx.document);
    emit repaintRequested();
    return event.isLeft();
}

void MoveTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return;
    m_dragging = false;
    m_movingMaskOnly = false;
    finishDrag(ctx.document);
}

} // namespace Ps
