/**
 * movetool.cpp — 移动工具实现（tools 层）。
 *
 * 对照 GIMP gimpeditselectiontool_update_motion：
 * translate live_items → gimp_projection_flush（非 image_flush）。
 * preview_freeze 只抑制缩略图，画布走 Projection 脏区 + idle。
 */
#include "movetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"

#include <QPainter>
#include <QPen>
#include <QtMath>

namespace Ps {

namespace {

const QColor kTransformBlue(26, 159, 255);
constexpr qreal kHandleSize = 7.0;

} // namespace

MoveTool::MoveTool(QObject *parent)
    : Tool(Ps::ToolId::Move, parent)
{
}

Qt::CursorShape MoveTool::cursorShape() const
{
    return Qt::SizeAllCursor;
}

void MoveTool::finishDrag(ImageDocument *doc)
{
    if (doc && doc->isPreviewFrozen())
        doc->endPreviewFreeze();
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

    // 对照 GIMP：gimp_viewable_preview_freeze — 只冻缩略图
    ctx.document->beginPreviewFreeze();

    // 取消链接 + 正在编辑蒙版 → 只移动蒙版内容（对照 PS）
    if (ctx.document->isEditingLayerMask()
        && layer->mask() && !layer->mask()->isNull()
        && !layer->mask()->isLinked()) {
        m_movingMaskOnly = true;
        ctx.document->pushLayerPropertiesUndo(index, QObject::tr("移动蒙版"));
        emit repaintRequested();
        return true;
    }

    // 对照 GIMP first_move：undo 在 press 入组，后续 motion 只改 offset
    ctx.document->pushLayerOffsetUndo(index);
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

    // 对照 GIMP update_motion：gimp_image_item_list_translate + projection_flush
    if (m_movingMaskOnly)
        ctx.document->shiftLayerMask(m_layerIndex, dx, dy);
    else
        ctx.document->translateLayer(m_layerIndex, dx, dy);

    m_lastImagePos += QPointF(dx, dy);
    // 变换框浮层随 offset 更新（像素已由 contentChanged → 投影刷新）
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
    // 对照 GIMP：thaw + gimp_image_flush
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
