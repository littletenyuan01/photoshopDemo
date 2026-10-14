/**
 * compositegraph.cpp — compositegraph.h 实现（engine/graph 层）。
 *
 * 定义图层栅格源 / 蒙版源 / 透明源 / 调整层应用算子，并把图层栈编成 DAG。
 */
#include "compositegraph.h"

#include "blendop.h"
#include "graphsource.h"
#include "pointfilterop.h"
#include "sourceop.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"

#include <QImage>
#include <QPainter>
#include <QRect>
#include <memory>

namespace Ps {
namespace {

/** 透明源：任意区域返回全透明（合成累加器起点）。 */
class TransparentSource : public GraphSource
{
public:
    QImage read(const QRect &rect) const override
    {
        QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        return out;
    }
};

/** 图层栅格源：把 ensureCompositeRaster 的图放到文档坐标 (docOx, docOy)。 */
class LayerRasterSource : public GraphSource
{
public:
    LayerRasterSource(const QImage &raster, int docOx, int docOy)
        : m_raster(raster), m_docOx(docOx), m_docOy(docOy) {}

    QImage read(const QRect &rect) const override
    {
        QImage out(rect.size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        if (m_raster.isNull())
            return out;

        const QRect full(m_docOx, m_docOy, m_raster.width(), m_raster.height());
        const QRect srcRect = full.intersected(rect);
        if (srcRect.isEmpty())
            return out;

        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(srcRect.topLeft() - rect.topLeft(), m_raster,
                    QRect(srcRect.left() - m_docOx, srcRect.top() - m_docOy,
                          srcRect.width(), srcRect.height()));
        p.end();
        return out;
    }

private:
    QImage m_raster;
    int m_docOx = 0;
    int m_docOy = 0;
};

/** 蒙版源：层局部坐标 → 灰度（文档坐标区域）；越界 = 0（全藏，与 LayerMask::valueAt 一致）。 */
class MaskSource : public GraphSource
{
public:
    MaskSource(const LayerMask *mask, int layerOx, int layerOy)
        : m_mask(mask), m_layerOx(layerOx), m_layerOy(layerOy) {}

    QImage read(const QRect &rect) const override
    {
        QImage out(rect.size(), QImage::Format_Grayscale8);
        out.fill(0);
        if (!m_mask)
            return out;
        for (int y = 0; y < rect.height(); ++y) {
            uchar *line = out.scanLine(y);
            for (int x = 0; x < rect.width(); ++x)
                line[x] = m_mask->valueAt(rect.left() + x - m_layerOx,
                                          rect.top() + y - m_layerOy);
        }
        return out;
    }

private:
    const LayerMask *m_mask;
    int m_layerOx = 0;
    int m_layerOy = 0;
};

/**
 * 调整层应用算子：输入 0 = below（滤镜前下方合成），输入 1 = adjusted（滤镜后）。
 * 按 opacity × mask 对预乘像素做逐分量 lerp（对照 Compositor 调整层路径）。
 */
class AdjustmentApplyOp : public GraphOp
{
public:
    AdjustmentApplyOp(qreal opacity, std::shared_ptr<const GraphSource> mask)
        : m_opacity(opacity), m_mask(std::move(mask)) {}

    int inputCount() const override { return 2; }

    QVector<QRect> requiredInputs(const QRect &out) const override { return { out, out }; }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        const QImage below = ins.value(0);
        const QImage adjusted = ins.value(1);
        if (adjusted.isNull())
            return below;
        if (below.isNull())
            return adjusted;

        QImage result = below;
        if (result.format() != QImage::Format_ARGB32_Premultiplied)
            result = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        const QImage maskImg = m_mask ? m_mask->read(out) : QImage();
        const int w = out.width();
        const int h = out.height();
        for (int y = 0; y < h; ++y) {
            QRgb *dline = reinterpret_cast<QRgb *>(result.scanLine(y));
            const QRgb *bline = reinterpret_cast<const QRgb *>(below.constScanLine(y));
            const QRgb *fline = reinterpret_cast<const QRgb *>(adjusted.constScanLine(y));
            const uchar *mline = nullptr;
            if (!maskImg.isNull() && maskImg.format() == QImage::Format_Grayscale8)
                mline = maskImg.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                qreal t = m_opacity;
                if (mline)
                    t *= mline[x] / 255.0;
                if (t <= 0.0)
                    continue;
                if (t >= 0.999) {
                    dline[x] = fline[x];
                    continue;
                }
                const QRgb d = bline[x];
                const QRgb f = fline[x];
                dline[x] = qRgba(
                    qBound(0, int(qRed(d) + (qRed(f) - qRed(d)) * t + 0.5), 255),
                    qBound(0, int(qGreen(d) + (qGreen(f) - qGreen(d)) * t + 0.5), 255),
                    qBound(0, int(qBlue(d) + (qBlue(f) - qBlue(d)) * t + 0.5), 255),
                    qBound(0, int(qAlpha(d) + (qAlpha(f) - qAlpha(d)) * t + 0.5), 255));
            }
        }
        return result;
    }

private:
    qreal m_opacity;
    std::shared_ptr<const GraphSource> m_mask;
};

/** 解析层的有效蒙版源（对照 Compositor 的 mask 解析：全白蒙版当无蒙版）。 */
std::shared_ptr<const GraphSource> makeMaskSource(const Layer *layer)
{
    const LayerMask *raw = (layer->hasMask() && layer->mask() && layer->mask()->isEnabled())
                               ? layer->mask()
                               : nullptr;
    const LayerMask *mask = (raw && !raw->isFullyOpaque()) ? raw : nullptr;
    if (!mask)
        return nullptr;
    return std::make_shared<MaskSource>(mask, layer->offsetX(), layer->offsetY());
}

} // namespace

GraphNode::Id CompositeGraph::build(PsGraph &graph, const ImageDocument &doc)
{
    // 累加器起点：透明源
    GraphNode::Id acc = graph.addNode(
        std::make_shared<SourceOp>(std::make_shared<TransparentSource>()));

    const LayerStack &stack = doc.layers();
    for (int i = 0; i < stack.count(); ++i) {
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;

        if (layer->isAdjustmentLayer()) {
            // 调整层：滤镜链作用于下方合成结果，再按 opacity × mask 写回
            if (!layer->filters().hasEnabled())
                continue;

            GraphNode::Id cur = acc;
            const QVector<FilterNode> nodes = layer->filters().snapshot();
            for (const FilterNode &node : nodes) {
                if (!node.isEnabled())
                    continue;
                const GraphNode::Id f = graph.addNode(std::make_shared<PointFilterOp>(node));
                graph.connect(cur, f, 0);
                cur = f;
            }

            const GraphNode::Id apply = graph.addNode(
                std::make_shared<AdjustmentApplyOp>(layer->opacity(), makeMaskSource(layer)));
            graph.connect(acc, apply, 0);   // below（滤镜前）
            graph.connect(cur, apply, 1);   // adjusted（滤镜后）
            acc = apply;
            continue;
        }

        // 栅格层：有活动预览时与 Compositor 同口径（瓦片 + preview），否则整层烘焙（A33）
        const bool hasPreview = layer->hasCompositePreview();
        if (!layer->hasPixelData() && !hasPreview)
            continue;

        QImage rasterImg;
        int docOx = 0;
        int docOy = 0;
        if (hasPreview) {
            // 合成层局部瓦片 + 文档坐标 preview → 一张栅格源
            QRect cover = layer->boundsInDocument();
            const QImage &prev = layer->compositePreview();
            if (!prev.isNull()) {
                cover = cover.united(QRect(layer->compositePreviewDocX(),
                                           layer->compositePreviewDocY(),
                                           prev.width(), prev.height()));
            }
            if (cover.isEmpty())
                continue;
            rasterImg = QImage(cover.size(), QImage::Format_ARGB32_Premultiplied);
            rasterImg.fill(Qt::transparent);
            if (layer->hasPixelData()) {
                const QImage tiles = layer->materialize();
                QPainter p(&rasterImg);
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.drawImage(layer->offsetX() - cover.left(),
                            layer->offsetY() - cover.top(), tiles);
            }
            if (!prev.isNull()) {
                QPainter p(&rasterImg);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                p.drawImage(layer->compositePreviewDocX() - cover.left(),
                            layer->compositePreviewDocY() - cover.top(), prev);
            }
            docOx = cover.left();
            docOy = cover.top();
        } else {
            const Layer::CompositeRaster raster = layer->ensureCompositeRaster();
            if (raster.image.isNull())
                continue;
            rasterImg = raster.image;
            docOx = layer->offsetX() + raster.originDx;
            docOy = layer->offsetY() + raster.originDy;
        }

        const GraphNode::Id src = graph.addNode(std::make_shared<SourceOp>(
            std::make_shared<LayerRasterSource>(rasterImg, docOx, docOy)));

        const GraphNode::Id blend = graph.addNode(std::make_shared<BlendOp>(
            layer->blendMode(), layer->opacity(), makeMaskSource(layer)));
        graph.connect(acc, blend, 0);  // backdrop
        graph.connect(src, blend, 1);  // layer
        acc = blend;
    }

    return acc;
}

} // namespace Ps
