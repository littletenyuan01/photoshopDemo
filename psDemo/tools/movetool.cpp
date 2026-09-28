#include "movetool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QPen>
#include <QtMath>

namespace Ps {

namespace {

/** PS 变换控件描边蓝（近似 #1a9fff）。 */
const QColor kTransformBlue(26, 159, 255);
/** 锚点边长（控件像素，不随缩放变）。 */
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

void MoveTool::drawOverlay(QPainter &painter, const ToolContext &ctx) const
{
    // 【功能】活动层内容包围盒 + 8 锚点（只显示，不交互）
    // 【对照】PS 移动工具「显示变换控件」；框为非透明像素 AABB
    if (!ctx.document)
        return;

    const Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return;

    const QRect content = layer->contentBoundsInDocument();
    if (content.isEmpty())
        return;

    const QPointF tl = ctx.imageToWidget(QPointF(content.left(), content.top()));
    const QPointF br = ctx.imageToWidget(QPointF(content.left() + content.width(),
                                                 content.top() + content.height()));
    // 用 QRectF(tl, br) 在 br<tl（极端）时也能 normalize；宽高为文档像素边在视口中的跨度
    QRectF box(tl, br);
    box = box.normalized();
    if (box.width() < 0.5 && box.height() < 0.5)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    QPen boxPen(kTransformBlue);
    boxPen.setCosmetic(true);
    boxPen.setWidth(1);
    painter.setPen(boxPen);
    painter.setBrush(Qt::NoBrush);
    // 画在像素边界上：inset 半像素，避免与内容糊在一起
    painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));

    const qreal h = kHandleSize;
    const qreal half = h * 0.5;
    const QPointF centers[8] = {
        box.topLeft(),
        QPointF(box.center().x(), box.top()),
        box.topRight(),
        QPointF(box.right(), box.center().y()),
        box.bottomRight(),
        QPointF(box.center().x(), box.bottom()),
        box.bottomLeft(),
        QPointF(box.left(), box.center().y()),
    };

    painter.setBrush(Qt::white);
    painter.setPen(boxPen);
    for (const QPointF &c : centers) {
        painter.drawRect(QRectF(c.x() - half, c.y() - half, h, h));
    }

    painter.restore();
}

bool MoveTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;
    if (!ctx.document)
        return false;

    // 对照 gimp_move_tool（!move_current）：先 gimp_image_pick_layer，再设为选中
    const int docX = qFloor(event.imagePos.x());
    const int docY = qFloor(event.imagePos.y());
    const int picked = ctx.document->pickLayerAt(docX, docY);
    if (picked >= 0)
        ctx.document->setActiveLayerIndex(picked);

    Layer *layer = ctx.document->activeLayer();
    if (!layer || !layer->isVisible())
        return false;

    m_dragging = true;
    m_lastImagePos = event.imagePos;
    return true;
}

bool MoveTool::mouseMove(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    if (!(event.buttons & Qt::LeftButton))
        return false;
    if (!ctx.document)
        return false;

    const int index = ctx.document->activeLayerIndex();
    if (index < 0)
        return false;

    // 整像素步进（对照 GIMP SIGNED_ROUND 后的整数平移）
    const int dx = qRound(event.imagePos.x() - m_lastImagePos.x());
    const int dy = qRound(event.imagePos.y() - m_lastImagePos.y());
    if (dx == 0 && dy == 0)
        return true;

    ctx.document->translateLayer(index, dx, dy);
    m_lastImagePos += QPointF(dx, dy);
    emit repaintRequested();
    return true;
}

bool MoveTool::mouseRelease(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    if (!m_dragging)
        return false;
    m_dragging = false;
    return event.isLeft();
}

void MoveTool::deactivate(const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(ctx)
    Q_UNUSED(view)
    m_dragging = false;
}

} // namespace Ps
