/**
 * compositor.cpp — compositor.h 实现（engine 层）。
 *
 * blendTileOnto 逐瓦片解预乘 → Blend::pixel → composite_union 写回。
 */
#include "compositor.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "domain/tilebuffer.h"
#include "engine/blend.h"
#include "engine/cache/tilecache.h"
#include "engine/op/layermodecatalog.h"
#include "engine/op/layermodeop.h"
#include "engine/op/pointopregistry.h"
#include "engine/premul.h"
#include "engine/compositepixel.h"
#include "engine/graph/compositegraph.h"
#include "engine/graph/psgraph.h"

#include <QAtomicInt>
#include <QPainter>
#include <QRect>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtGlobal>

#include <cstring>

namespace Ps {

namespace {

/**
 * 把源瓦片按混合模式画到 dst（= 下方已合成结果，预乘 ARGB32）。
 *
 * 【对照 GIMP】
 * - 颜色 B(Cb, Cs)：`LayerModeOp` ⇔ `gimpoperationlayermode-blend.c`
 * - Alpha 合成：本函数 ⇔ `gimpoperationlayermode-composite.c` 的 `composite_union`
 *
 * @param mode 混合模式（**按值**传入）。不再依赖常驻 `LayerModeOp` 实例：
 *        那是共享可变状态，按 chunk 并行合成会竞态；颜色算法本就在 `Blend::pixel`。
 */
/**
 * 把源瓦片混到 @p dst。
 * @p dstOrigin 为 dst(0,0) 对应的**文档坐标**；整幅投影时为 (0,0)，
 * 调整层面积算子的子缓冲则为 srcWant.topLeft()（只分配外扩矩形，不建整幅临时图）。
 */
void blendTileOnto(QImage &dst,
                   const QImage &src,
                   int ox,
                   int oy,
                   qreal opacity,
                   BlendMode mode,
                   const QRect &dirtyDoc,
                   const LayerMask *mask,
                   int layerOx,
                   int layerOy,
                   QPoint dstOrigin = QPoint(0, 0))
{
    if (opacity <= 0.0 || src.isNull())
        return;

    const QRect dstDocRect(dstOrigin.x(), dstOrigin.y(), dst.width(), dst.height());
    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirtyDoc.intersected(dstDocRect).intersected(tileRect);
    if (area.isEmpty())
        return;

    const bool dissolving = (mode == BlendMode::Dissolve);
    const bool useMask = mask && mask->isEnabled() && !mask->isNull();

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y - dstOrigin.y()))
                      + (area.left() - dstOrigin.x());
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(y - oy))
                            + (area.left() - ox);

        for (int x = 0; x < area.width(); ++x) {
            const int docX = area.left() + x;
            const int maskV = useMask ? int(mask->valueAt(docX - layerOx, y - layerOy)) : -1;
            dline[x] = compositePixelOnto(dline[x], sline[x], opacity, mode,
                                          dissolving, docX, y, maskV);
        }
    }
}

/**
 * 叠层实现：@p dst 可以是整幅投影，也可以是「文档矩形 @p rect」对应的子图
 * （此时 @p dstOrigin 必须等于 rect.topLeft()，且 dst.size()==rect.size()）。
 */
bool blendLayerRangeImpl(QImage &dst,
                         const ImageDocument &doc,
                         const QRect &rect,
                         int layerBegin,
                         int layerEnd,
                         int skipLayer,
                         QPoint dstOrigin)
{
    const QRect dstDocRect(dstOrigin.x(), dstOrigin.y(), dst.width(), dst.height());
    const QRect area = rect.intersected(dstDocRect)
                           .intersected(QRect(0, 0, doc.width(), doc.height()));
    if (area.isEmpty())
        return true;

    const LayerStack &stack = doc.layers();
    const int lo = qMax(0, layerBegin);
    const int hi = qMin(stack.count(), layerEnd);
    for (int i = lo; i < hi; ++i) {
        if (i == skipLayer)
            continue;
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;

        // 调整层：滤镜作用在下方阶段性合成上，再按不透明度/蒙版写回
        if (layer->isAdjustmentLayer()) {
            if (!layer->filters().hasEnabled())
                continue;

            const QRect docBounds(0, 0, doc.width(), doc.height());
            // 文档坐标：面积算子要外扩邻居；不能从共享投影读 pad（邻块可能未合成/并行冲突）
            const QRect srcWant =
                layer->filters().requiredInputRect(area).intersected(docBounds);

            QImage filtered;
            if (srcWant == area && dstOrigin == QPoint(0, 0)
                && dst.width() == doc.width() && dst.height() == doc.height()) {
                // 点算子快路径：下方结果已在投影 dst 的 area 里
                filtered = layer->filters().apply(dst.copy(area));
            } else {
                QImage below(srcWant.size(), QImage::Format_ARGB32_Premultiplied);
                below.fill(Qt::transparent);
                if (!blendLayerRangeImpl(below, doc, srcWant, lo, i, skipLayer,
                                         srcWant.topLeft()))
                    return false;
                QImage work = layer->filters().apply(below, srcWant, area, nullptr);
                if (work.isNull())
                    continue;
                const int dx = area.left() - srcWant.left();
                const int dy = area.top() - srcWant.top();
                if (work.width() == area.width() && work.height() == area.height()
                    && dx == 0 && dy == 0) {
                    filtered = work;
                } else {
                    filtered = work.copy(dx, dy, area.width(), area.height());
                }
            }
            if (filtered.isNull())
                continue;

            const LayerMask *rawMask = (layer->hasMask() && layer->mask()
                                        && layer->mask()->isEnabled())
                                           ? layer->mask()
                                           : nullptr;
            const LayerMask *mask =
                (rawMask && !rawMask->isFullyOpaque()) ? rawMask : nullptr;
            const qreal opacity = layer->opacity();
            const int layerOx = layer->offsetX();
            const int layerOy = layer->offsetY();

            if (opacity >= 0.999 && !mask) {
                const int copyW = qMin(area.width(), filtered.width());
                const int copyH = qMin(area.height(), filtered.height());
                for (int y = 0; y < copyH; ++y) {
                    QRgb *dline =
                        reinterpret_cast<QRgb *>(dst.scanLine(area.top() + y - dstOrigin.y()))
                        + (area.left() - dstOrigin.x());
                    const QRgb *fline =
                        reinterpret_cast<const QRgb *>(filtered.constScanLine(y));
                    std::memcpy(dline, fline, size_t(copyW) * sizeof(QRgb));
                }
            } else {
                for (int y = area.top(); y <= area.bottom(); ++y) {
                    QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y - dstOrigin.y()))
                                  + (area.left() - dstOrigin.x());
                    const QRgb *fline =
                        reinterpret_cast<const QRgb *>(filtered.constScanLine(y - area.top()));
                    for (int x = 0; x < area.width(); ++x) {
                        qreal t = opacity;
                        if (mask) {
                            const int lx = (area.left() + x) - layerOx;
                            const int ly = y - layerOy;
                            t *= mask->valueAt(lx, ly) / 255.0;
                        }
                        if (t <= 0.0)
                            continue;
                        if (t >= 0.999) {
                            dline[x] = fline[x];
                            continue;
                        }
                        const QRgb d = dline[x];
                        const QRgb f = fline[x];
                        const int or_ = qBound(0, int(qRed(d) + (qRed(f) - qRed(d)) * t + 0.5), 255);
                        const int og = qBound(0, int(qGreen(d) + (qGreen(f) - qGreen(d)) * t + 0.5), 255);
                        const int ob = qBound(0, int(qBlue(d) + (qBlue(f) - qBlue(d)) * t + 0.5), 255);
                        const int oa = qBound(0, int(qAlpha(d) + (qAlpha(f) - qAlpha(d)) * t + 0.5), 255);
                        dline[x] = qRgba(or_, og, ob, oa);
                    }
                }
            }
            continue;
        }

        const bool hasPreview = layer->hasCompositePreview();
        if (!layer->hasPixelData() && !hasPreview)
            continue;

        const BlendMode mode = layer->blendMode();
        if (!PointOpRegistry::find(layerModeOperation(mode)))
            continue;

        const int layerOx = layer->offsetX();
        const int layerOy = layer->offsetY();
        const qreal opacity = layer->opacity();
        const LayerMask *rawMask = (layer->hasMask() && layer->mask()
                                    && layer->mask()->isEnabled())
                                       ? layer->mask()
                                       : nullptr;
        const LayerMask *mask =
            (rawMask && !rawMask->isFullyOpaque()) ? rawMask : nullptr;

        const QRect localArea = area.translated(-layerOx, -layerOy);

        if (hasPreview) {
            if (layer->hasPixelData()) {
                layer->tiles().forEachTileInRect(
                    localArea,
                    [&](int, int, const QImage &tile, const QRect &bounds) {
                        blendTileOnto(dst, tile,
                                      bounds.x() + layerOx,
                                      bounds.y() + layerOy,
                                      opacity, mode, area,
                                      mask, layerOx, layerOy, dstOrigin);
                    });
            }
            blendTileOnto(dst, layer->compositePreview(),
                          layer->compositePreviewDocX(),
                          layer->compositePreviewDocY(),
                          opacity, mode, area,
                          mask, layerOx, layerOy, dstOrigin);
            continue;
        }

        const bool needMaterialize = layer->filters().hasEnabled()
                                     || layer->styles().hasEnabled();
        if (needMaterialize) {
            const Layer::CompositeRegion r = layer->compositeRasterRegion(area);
            if (!r.image.isNull()) {
                blendTileOnto(dst, r.image, r.docX, r.docY,
                              opacity, mode, area,
                              mask, layerOx, layerOy, dstOrigin);
            }
            continue;
        }

        layer->tiles().forEachTileInRect(
            localArea,
            [&](int, int, const QImage &tile, const QRect &bounds) {
                blendTileOnto(dst, tile,
                              bounds.x() + layerOx,
                              bounds.y() + layerOy,
                              opacity, mode, area,
                              mask, layerOx, layerOy, dstOrigin);
            });
    }

    return true;
}

} // namespace

QImage Compositor::composite(const ImageDocument &doc)
{
    return composite(doc, QRect(0, 0, doc.width(), doc.height()));
}

QImage Compositor::composite(const ImageDocument &doc, const QRect &rect)
{
    QImage result(doc.width(), doc.height(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    compositeRegion(result, doc, rect);
    return result;
}

bool Compositor::compositeRegion(QImage &dst, const ImageDocument &doc, const QRect &rect)
{
    if (dst.isNull()
        || dst.width() != doc.width()
        || dst.height() != doc.height()
        || dst.format() != QImage::Format_ARGB32_Premultiplied) {
        return false;
    }

    const QRect area = rect.intersected(dst.rect());
    if (area.isEmpty())
        return true;

    // 投影该矩形从零叠起（对照投影 invalidate 后重算）。
    // 直接逐行清零，不用 QPainter：分块重算时每 chunk 建一次 QPainter 很贵；
    // 且只写本矩形内的行段，便于将来按不相交区域并行。
    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        std::memset(line, 0, size_t(area.width()) * sizeof(QRgb));
    }

    return blendLayerRange(dst, doc, area, 0, doc.layers().count(), -1);
}

bool Compositor::blendLayerRange(QImage &dst,
                                 const ImageDocument &doc,
                                 const QRect &rect,
                                 int layerBegin,
                                 int layerEnd,
                                 int skipLayer)
{
    if (dst.isNull()
        || dst.width() != doc.width()
        || dst.height() != doc.height()
        || dst.format() != QImage::Format_ARGB32_Premultiplied) {
        return false;
    }

    return blendLayerRangeImpl(dst, doc, rect, layerBegin, layerEnd, skipLayer,
                               QPoint(0, 0));
}

void Compositor::warmLayerCaches(const ImageDocument &doc)
{
    const LayerStack &stack = doc.layers();
    for (int i = 0; i < stack.count(); ++i) {
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;
        // 只有「有启用样式」的层还走整层 mutable 缓存（compositeRasterRegion 对有样式的
        // 层退回 ensureCompositeRaster）。纯滤镜层已按区域求值、不碰该缓存，不必预热。
        // 调整层本来就不走这条路（它滤的是下方结果，不是自身空栅格）。
        if (!layer->isAdjustmentLayer() && layer->styles().hasEnabled())
            (void)layer->ensureCompositeRaster();
        // 有蒙版 → 预热「是否全白」判定（同为 mutable 缓存）
        if (layer->hasMask() && layer->mask() && layer->mask()->isEnabled())
            (void)layer->mask()->isFullyOpaque();
    }
}

bool Compositor::blendLayerRangeParallel(QImage &dst, const ImageDocument &doc,
                                         const QRect &rect, int layerBegin, int layerEnd,
                                         int skipLayer)
{
    if (dst.isNull())
        return false;
    const QRect area = rect.intersected(dst.rect());
    if (area.isEmpty())
        return false;

    // 块大小与投影一致（64）：共用同一套瓦片/脏块粒度
    constexpr int kChunk = 64;
    const int cols = (area.width() + kChunk - 1) / kChunk;
    const int rows = (area.height() + kChunk - 1) / kChunk;
    const int threads = qMax(1, QThreadPool::globalInstance()->maxThreadCount());

    if (cols * rows < 2 || threads <= 1)
        return blendLayerRange(dst, doc, area, layerBegin, layerEnd, skipLayer);

    // 并行前置：钉住+预加载瓦片（A5）+ 预热 mutable 缓存 + 确保 dst 独占
    TileSharedReadGuard sharedTiles(&doc);
    warmLayerCaches(doc);
    (void)dst.bits();

    QVector<QRect> pieces;
    pieces.reserve(cols * rows);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x = area.left() + c * kChunk;
            const int y = area.top() + r * kChunk;
            pieces.append(QRect(x, y, qMin(kChunk, area.right() - x + 1),
                                qMin(kChunk, area.bottom() - y + 1)));
        }
    }

    // 各块只读写自己那块 dst 区域，互不相交 → 可并行
    QAtomicInt ok(1);
    QtConcurrent::blockingMap(pieces, [&](const QRect &piece) {
        if (!blendLayerRange(dst, doc, piece, layerBegin, layerEnd, skipLayer))
            ok.storeRelaxed(0);
    });
    return ok.loadRelaxed() != 0;
}

QImage Compositor::compositeViaGraph(const ImageDocument &doc, const QRect &rect)
{
    PsGraph graph;
    const GraphNode::Id sink = CompositeGraph::build(graph, doc);
    const QImage region = graph.evaluate(sink, rect);

    QImage out(doc.width(), doc.height(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (!region.isNull()) {
        QPainter p(&out);
        p.drawImage(rect.topLeft(), region);
        p.end();
    }
    return out;
}

} // namespace Ps
