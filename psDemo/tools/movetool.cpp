/**
 * movetool.cpp — 移动工具实现（tools 层）。
 */
#include "movetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"

#include <QPainter>
#include <QPen>
#include <QtMath>

#include <cstring>

namespace Ps {

namespace {

const QColor kTransformBlue(26, 159, 255);
constexpr qreal kHandleSize = 7.0;

void copyRect(QImage &dst, const QImage &src, const QRect &rect)
{
    const QRect area = rect.intersected(dst.rect()).intersected(src.rect());
    if (area.isEmpty())
        return;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(y)) + area.left();
        auto *d = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        std::memcpy(d, s, size_t(area.width()) * sizeof(QRgb));
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
    return (m_dragging && !m_live.isNull()) ? &m_live : nullptr;
}

void MoveTool::startDrag(ImageDocument &doc, int layerIndex)
{
    const QRect full(0, 0, doc.width(), doc.height());
    if (full.isEmpty() || layerIndex < 0)
        return;

    m_layerIndex = layerIndex;
    m_base = QImage(full.size(), QImage::Format_ARGB32_Premultiplied);
    m_base.fill(Qt::transparent);
    Compositor::blendLayerRange(m_base, doc, full, 0, doc.layers().count(), layerIndex);

    m_live = m_base.copy();
    Layer *layer = doc.layers().layerAt(layerIndex);
    m_blitRect = layer ? layer->styleBoundsInDocument().intersected(full) : QRect();
    if (!m_blitRect.isEmpty())
        Compositor::blendLayerRange(m_live, doc, m_blitRect, layerIndex, layerIndex + 1, -1);

    doc.beginPreviewFreeze();
}

void MoveTool::finishDrag(ImageDocument *doc)
{
    if (doc && doc->isPreviewFrozen())
        doc->endPreviewFreeze();
    m_layerIndex = -1;
    m_blitRect = {};
    m_base = {};
    m_live = {};
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
    m_lastImagePos = event.imagePos;
    ctx.document->pushLayerOffsetUndo(index);
    startDrag(*ctx.document, index);
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

    Layer *layer = ctx.document->layers().layerAt(m_layerIndex);
    const QRect docRect(0, 0, ctx.document->width(), ctx.document->height());
    const QRect before = layer ? layer->styleBoundsInDocument().intersected(docRect) : QRect();

    ctx.document->translateLayer(m_layerIndex, dx, dy);
    m_lastImagePos += QPointF(dx, dy);

    const QRect after = layer ? layer->styleBoundsInDocument().intersected(docRect) : QRect();
    const QRect dirty = before.united(after).united(m_blitRect).intersected(docRect);
    if (!dirty.isEmpty() && !m_live.isNull()) {
        copyRect(m_live, m_base, dirty);
        Compositor::blendLayerRange(m_live, *ctx.document, dirty,
                                    m_layerIndex, m_layerIndex + 1, -1);
        m_blitRect = dirty;
    }
    emit repaintRequested();
    return true;
}

bool MoveTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    m_dragging = false;
    finishDrag(ctx.document);
    return event.isLeft();
}

void MoveTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return;
    m_dragging = false;
    finishDrag(ctx.document);
}

} // namespace Ps
