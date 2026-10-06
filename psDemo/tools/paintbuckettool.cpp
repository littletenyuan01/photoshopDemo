/**
 * paintbuckettool.cpp — 油漆桶工具实现（tools 层）。
 */
#include "paintbuckettool.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/paintengine.h"
#include "toolcursor.h"

#include <QPoint>
#include <QtMath>

namespace Ps {

PaintBucketTool::PaintBucketTool(QObject *parent)
    : Tool(Ps::ToolId::PaintBucket, parent)
{
}

QCursor PaintBucketTool::cursor() const
{
    // 热点落在倾倒口/水滴附近（对齐 PS 油漆桶光标）
    return ToolCursor::fromToolIcon(QStringLiteral(":/icons/tools/bucket.png"),
                                    0.88, 0.90, 28);
}

bool PaintBucketTool::mousePress(const ToolEvent &event, const ToolContext &ctx, ViewPort &view)
{
    Q_UNUSED(view)
    if (!event.isLeft())
        return false;

    // 对照 gimp_bucket_fill_tool_button_press：隐藏层 / 链接层不可改
    Layer *layer = editableActiveLayer(ctx);
    if (!layer)
        return false;

    const QPointF local = layer->toLayerLocal(event.imagePos);
    const QPoint seed(qFloor(local.x()), qFloor(local.y()));
    if (seed.x() < 0 || seed.y() < 0
        || seed.x() >= layer->width() || seed.y() >= layer->height())
        return false;

    ctx.document->pushLayerPixelsUndo(ctx.document->activeLayerIndex(),
                                      QObject::tr("油漆桶"));

    // fill-mode：FG / BG（PATTERN 对应 GIMP_BUCKET_FILL_PATTERN，未实现 → 回退前景）
    QColor fill = ctx.foreground;
    switch (ctx.fillSource) {
    case FillSource::Background:
        fill = ctx.background;
        break;
    case FillSource::Foreground:
    case FillSource::Pattern:
        fill = ctx.foreground;
        break;
    }
    // opacity：GIMP 经 apply_buffer 的 context opacity；此处瘦身为改写颜色 alpha
    const int opacityQ = qBound(0, int(ctx.fillOpacity * 255.0 + 0.5), 255);
    fill.setAlpha((fill.alpha() * opacityQ + 127) / 255);

    // 对照 gimp_item_mask_intersect：空选区不裁；非空只填 mask 内
    PaintEngine::SelectionClip clip;
    clip.selection = &ctx.document->selection();
    clip.layerOffsetX = layer->offsetX();
    clip.layerOffsetY = layer->offsetY();
    const bool hadSelection = !ctx.document->selection().isEmpty();

    const QRect dirtyLocal = PaintEngine::floodFill(layer->tiles(), seed, fill,
                                                    ctx.fillTolerance, ctx.fillContiguous,
                                                    clip);
    if (dirtyLocal.isEmpty())
        return true; // 已消费点击，只是无需改像素

    // 脏区映回文档坐标
    markDocumentDirty(ctx, dirtyLocal.translated(layer->offsetX(), layer->offsetY()));

    // 填充用完选区后取消（本 Demo 约定；PS 默认保留选区）
    if (hadSelection)
        ctx.document->clearSelection();
    return true;
}

} // namespace Ps
