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
void blendTileOnto(QImage &dst,
                   const QImage &src,
                   int ox,
                   int oy,
                   qreal opacity,
                   BlendMode mode,
                   const QRect &dirty,
                   const LayerMask *mask,
                   int layerOx,
                   int layerOy)
{
    if (opacity <= 0.0 || src.isNull())
        return;

    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirty.intersected(dst.rect()).intersected(tileRect);
    if (area.isEmpty())
        return;

    const bool dissolving = (mode == BlendMode::Dissolve);
    const bool useMask = mask && mask->isEnabled() && !mask->isNull();

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
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

    const QRect area = rect.intersected(dst.rect());
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

        // 调整层：滤镜作用在下方阶段性合成（dst）上，再按不透明度/蒙版写回
        // 对照 PS 调整图层；GIMP 原生无此层种（drawable filter 只改本层像素）
        if (layer->isAdjustmentLayer()) {
            if (!layer->filters().hasEnabled())
                continue;

            QImage filtered = layer->filters().apply(dst.copy(area));
            if (filtered.isNull())
                continue;

            const LayerMask *rawMask = (layer->hasMask() && layer->mask()
                                        && layer->mask()->isEnabled())
                                           ? layer->mask()
                                           : nullptr;
            // 默认白蒙版（全 255）等价于无蒙版，走 QPainter 整区替换
            const LayerMask *mask =
                (rawMask && !rawMask->isFullyOpaque()) ? rawMask : nullptr;
            const qreal opacity = layer->opacity();
            const int layerOx = layer->offsetX();
            const int layerOy = layer->offsetY();

            // opacity≈1 且无有效蒙版：整区替换。
            // 直接逐行拷贝，不用 QPainter：同一 paint device 不能被多个 QPainter 同时绘制
            // （按 chunk 并行时会崩），而这里本就是 Source 模式的整幅贴图。
            if (opacity >= 0.999 && !mask) {
                const int copyW = qMin(area.width(), filtered.width());
                const int copyH = qMin(area.height(), filtered.height());
                for (int y = 0; y < copyH; ++y) {
                    QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(area.top() + y))
                                  + area.left();
                    const QRgb *fline =
                        reinterpret_cast<const QRgb *>(filtered.constScanLine(y));
                    std::memcpy(dline, fline, size_t(copyW) * sizeof(QRgb));
                }
            } else {
                for (int y = area.top(); y <= area.bottom(); ++y) {
                    QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y))
                                  + area.left();
                    const QRgb *fline =
                        reinterpret_cast<const QRgb *>(filtered.constScanLine(y - area.top()))
                        + 0;
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
                        const int dr = qRed(d);
                        const int dg = qGreen(d);
                        const int db = qBlue(d);
                        const int da = qAlpha(d);
                        const int fr = qRed(f);
                        const int fg = qGreen(f);
                        const int fb = qBlue(f);
                        const int fa = qAlpha(f);
                        const int or_ = qBound(0, int(dr + (fr - dr) * t + 0.5), 255);
                        const int og = qBound(0, int(dg + (fg - dg) * t + 0.5), 255);
                        const int ob = qBound(0, int(db + (fb - db) * t + 0.5), 255);
                        const int oa = qBound(0, int(da + (fa - da) * t + 0.5), 255);
                        dline[x] = qRgba(or_, og, ob, oa);
                    }
                }
            }
            continue;
        }

        const bool hasPreview = layer->hasCompositePreview();
        if (!layer->hasPixelData() && !hasPreview)
            continue;

        // 只做注册表「是否已初始化」的**只读**校验；不再取常驻 LayerModeOp 实例：
        // 那是共享可变状态（setMode 写 m_mode），并行合成必然竞态。颜色算法本就在
        // Blend::pixel，此处把 mode 作为值往下传（compositePixelOnto 同样按值收 mode）。
        const BlendMode mode = layer->blendMode();
        if (!PointOpRegistry::find(layerModeOperation(mode)))
            continue;

        const int layerOx = layer->offsetX();
        const int layerOy = layer->offsetY();
        const qreal opacity = layer->opacity();
        // 有蒙版且启用时乘到层 alpha（对照 GIMP Applicator aux / layer mask）
        // 全白蒙版跳过，避免无谓的 valueAt 乘算
        const LayerMask *rawMask = (layer->hasMask() && layer->mask()
                                    && layer->mask()->isEnabled())
                                       ? layer->mask()
                                       : nullptr;
        const LayerMask *mask =
            (rawMask && !rawMask->isFullyOpaque()) ? rawMask : nullptr;

        // 只遍历与本矩形相交的瓦片格（旧写法遍历该层**全部**瓦片再逐个判交，
        // 分块重算时是 O(块数 × 层数 × 总瓦片数)）
        const QRect localArea = area.translated(-layerOx, -layerOy);

        // 会话预览覆盖：瓦片（含挖空）+ 预览图叠在同一层位置，z 序不变
        if (hasPreview) {
            if (layer->hasPixelData()) {
                layer->tiles().forEachTileInRect(
                    localArea,
                    [&](int, int, const QImage &tile, const QRect &bounds) {
                        blendTileOnto(dst, tile,
                                      bounds.x() + layerOx,
                                      bounds.y() + layerOy,
                                      opacity, mode, area,
                                      mask, layerOx, layerOy);
                    });
            }
            blendTileOnto(dst, layer->compositePreview(),
                          layer->compositePreviewDocX(),
                          layer->compositePreviewDocY(),
                          opacity, mode, area,
                          mask, layerOx, layerOy);
            continue;
        }

        const bool needMaterialize = layer->filters().hasEnabled()
                                     || layer->styles().hasEnabled();
        if (needMaterialize) {
            // **按 area 区域**求值，而不是整层：整层粒度会让「画一笔」也重建全幅
            // 滤镜链（实测 447ms @2000x1500 + 3 点滤镜）。点滤镜逐像素，按区域与
            // 「整层后裁剪」逐像素等价；有样式时该函数内部退回整层缓存。
            const Layer::CompositeRegion r = layer->compositeRasterRegion(area);
            if (!r.image.isNull()) {
                blendTileOnto(dst, r.image, r.docX, r.docY,
                              opacity, mode, area,
                              mask, layerOx, layerOy);
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
                              mask, layerOx, layerOy);
            });
    }

    return true;
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

    // 并行前置（与投影分块合成同一套）：主线程预热 mutable 缓存 + 确保 dst 独占
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
