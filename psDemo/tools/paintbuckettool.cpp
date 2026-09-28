#include "paintbuckettool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"

#include <QPoint>
#include <QtMath>

namespace Ps {

PaintBucketTool::PaintBucketTool(QObject *parent)
    : Tool(Ps::ToolId::PaintBucket, parent)
{
}

Qt::CursorShape PaintBucketTool::cursorShape() const
{
    return Qt::PointingHandCursor;
}

bool PaintBucketTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    // 对照 gimp_bucket_fill_tool_button_press：隐藏层不可改（GIMP 另有 edit_non_visible 配置，此处固定拒绝）
    Layer *layer = ctx.document ? ctx.document->activeLayer() : nullptr;
    if (!layer || !layer->isVisible())
        return false;

    const QPointF local = layer->toLayerLocal(event.imagePos);
    const QPoint seed(qFloor(local.x()), qFloor(local.y()));
    if (seed.x() < 0 || seed.y() < 0
        || seed.x() >= layer->width() || seed.y() >= layer->height())
        return false;

    // fill-mode：FG / BG（PATTERN 对应 GIMP_BUCKET_FILL_PATTERN，未实现）
    QColor fill = (ctx.fillSource == 1) ? ctx.background : ctx.foreground;
    // opacity：GIMP 经 apply_buffer 的 context opacity；此处瘦身为改写颜色 alpha
    const int opacityQ = qBound(0, int(ctx.fillOpacity * 255.0 + 0.5), 255);
    fill.setAlpha((fill.alpha() * opacityQ + 127) / 255);

    const QRect dirtyLocal = PaintEngine::floodFill(layer->tiles(), seed, fill,
                                                    ctx.fillTolerance, ctx.fillContiguous);
    if (dirtyLocal.isEmpty())
        return true; // 已消费点击，只是无需改像素

    // 脏区映回文档坐标
    markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));
    return true;
}

} // namespace Ps
