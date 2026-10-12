/**
 * cpurasterbackend.cpp — 搬入原 FilterStack::apply 的 ③–⑦（gpu-plan.md §2.1 / §3.2）。
 */
#include "cpurasterbackend.h"

#include "domain/filterstack.h"
#include "engine/graph/pointfilterop.h"
#include "engine/graph/psgraph.h"
#include "engine/graph/sourceop.h"

#include <memory>

namespace Ps {

bool CpuRasterBackend::supports(const FilterStack &) const
{
    return true;
}

ApplyStackResult CpuRasterBackend::applyStack(QImage &image, const FilterStack &stack,
                                              const QRect &srcRect, const QRect &outRect,
                                              const std::function<bool()> &shouldAbort)
{
    if (image.isNull() || !stack.hasEnabled())
        return ApplyStackResult::Ok;

    // 调用方（FilterStack）已转成预乘；此处再兜底一次。
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const QRect srcIn = srcRect.isNull() ? image.rect() : srcRect;
    const QRect dstIn = outRect.isNull() ? image.rect() : outRect;
    Q_ASSERT_X(image.rect().contains(srcIn.translated(-srcIn.topLeft()))
                   && image.rect().contains(dstIn.translated(-srcIn.topLeft())),
               "CpuRasterBackend::applyStack",
               "srcRect/outRect（文档坐标）的尺寸必须与输入图自洽，且 srcRect 包住 outRect");

    PsGraph graph;
    GraphNode::Id prev = graph.addNode(std::make_shared<ImageSourceOp>(image));
    for (int i = 0; i < stack.count(); ++i) {
        const FilterNode &node = stack.at(i);
        if (!node.isEnabled())
            continue;
        if (shouldAbort && shouldAbort()) {
            image = QImage();
            return ApplyStackResult::Cancelled;
        }
        auto op = std::make_shared<PointFilterOp>(node);
        op->setShouldAbort(shouldAbort);
        op->setGeometry(srcIn, dstIn);
        const GraphNode::Id cur = graph.addNode(op);
        graph.connect(prev, cur, 0);
        prev = cur;
    }

    image = graph.evaluate(prev, image.rect());
    if (image.isNull()) {
        // evaluate 在取消路径可能交回空图
        return ApplyStackResult::Cancelled;
    }
    return ApplyStackResult::Ok;
}

} // namespace Ps
