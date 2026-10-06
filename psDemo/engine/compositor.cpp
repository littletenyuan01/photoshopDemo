/**
 * compositor.cpp — compositor.h 实现（engine 层）。
 *
 * blendTileOnto 逐瓦片解预乘 → LayerModeOp::blendPixel → composite_union 写回。
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

#include <QPainter>
#include <QRect>
#include <QtGlobal>

namespace Ps {

namespace {

/**
 * 把源瓦片按混合模式画到 dst（= 下方已合成结果，预乘 ARGB32）。
 *
 * 【对照 GIMP】
 * - 颜色 B(Cb, Cs)：`LayerModeOp` ⇔ `gimpoperationlayermode-blend.c`
 * - Alpha 合成：本函数 ⇔ `gimpoperationlayermode-composite.c` 的 `composite_union`
 *
 * @param modeOp 调用方**每层**解析一次的常驻算子。
 *        本函数逐瓦片调用，绝不能在这里取算子（旧代码每瓦片一次堆分配 +
 *        QHash 查找；对照 GIMP：per-mode op 缓存，见 gimp_layer_mode_get_operation）。
 */
void blendTileOnto(QImage &dst,
                   const QImage &src,
                   int ox,
                   int oy,
                   qreal opacity,
                   LayerModeOp *modeOp,
                   const QRect &dirty,
                   const LayerMask *mask,
                   int layerOx,
                   int layerOy)
{
    if (opacity <= 0.0 || src.isNull() || !modeOp)
        return;

    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirty.intersected(dst.rect()).intersected(tileRect);
    if (area.isEmpty())
        return;

    const float opacityF = float(opacity);
    const bool dissolving = (modeOp->mode() == BlendMode::Dissolve);
    const bool useMask = mask && mask->isEnabled() && !mask->isNull();

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(y - oy))
                            + (area.left() - ox);

        for (int x = 0; x < area.width(); ++x) {
            const QRgb sp = sline[x];
            int sr = 0, sg = 0, sb = 0, sa = 0;
            Premul::unpremultiplyRgb(sp, &sr, &sg, &sb, &sa);
            if (sa == 0)
                continue;

            if (useMask) {
                const int lx = (area.left() + x) - layerOx;
                const int ly = y - layerOy;
                sa = (sa * int(mask->valueAt(lx, ly)) + 127) / 255;
                if (sa == 0)
                    continue;
            }

            const QRgb dp = dline[x];
            int dr = 0, dg = 0, db = 0, da = 0;
            Premul::unpremultiplyRgb(dp, &dr, &dg, &db, &da);

            float la = (sa / 255.0f) * opacityF;
            if (dissolving) {
                if (!Blend::dissolveKeeps(area.left() + x, y, sa, opacity))
                    continue;
                la = 1.0f;
            }
            if (la <= 0.0f)
                continue;

            const float cs[3] = { sr / 255.0f, sg / 255.0f, sb / 255.0f };
            const float cb[3] = { dr / 255.0f, dg / 255.0f, db / 255.0f };

            float out[3];
            const float ia = da / 255.0f;
            const float ar01 = la + (1.0f - la) * ia;

            if (ia <= 0.0f) {
                out[0] = cs[0];
                out[1] = cs[1];
                out[2] = cs[2];
            } else {
                const int backdrop[3] = { dr, dg, db };
                const int source[3] = { sr, sg, sb };
                float comp[3];
                modeOp->blendPixel(backdrop, source, comp);

                const float ratio = la / ar01;
                for (int c = 0; c < 3; ++c)
                    out[c] = ratio * (ia * (comp[c] / 255.0f - cs[c]) + cs[c] - cb[c]) + cb[c];
            }

            const int ar = qBound(0, int(ar01 * 255.0f + 0.5f), 255);
            if (ar <= 0) {
                dline[x] = 0;
                continue;
            }

            const int pr = qBound(0, int(out[0] * ar + 0.5f), 255);
            const int pg = qBound(0, int(out[1] * ar + 0.5f), 255);
            const int pb = qBound(0, int(out[2] * ar + 0.5f), 255);
            dline[x] = qRgba(pr, pg, pb, ar);
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

    // 投影该矩形从零叠起（对照投影 invalidate 后重算）
    {
        QPainter painter(&dst);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(area, Qt::transparent);
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

            const LayerMask *mask = (layer->hasMask() && layer->mask()
                                     && layer->mask()->isEnabled())
                                        ? layer->mask()
                                        : nullptr;
            const qreal opacity = layer->opacity();
            const int layerOx = layer->offsetX();
            const int layerOy = layer->offsetY();

            // opacity≈1 且无蒙版：整区替换；否则预乘插值（保留下方透出）
            if (opacity >= 0.999 && !mask) {
                QPainter painter(&dst);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.drawImage(area.topLeft(), filtered);
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

        if (!layer->hasPixelData())
            continue;

        const BlendMode mode = layer->blendMode();
        auto *modeOp = static_cast<LayerModeOp *>(
            PointOpRegistry::instance(layerModeOperation(mode)));
        if (!modeOp)
            continue;
        modeOp->setMode(mode);

        const int layerOx = layer->offsetX();
        const int layerOy = layer->offsetY();
        const qreal opacity = layer->opacity();
        // 有蒙版且启用时乘到层 alpha（对照 GIMP Applicator aux / layer mask）
        const LayerMask *mask = (layer->hasMask() && layer->mask()
                                 && layer->mask()->isEnabled())
                                    ? layer->mask()
                                    : nullptr;

        const bool needMaterialize = layer->filters().hasEnabled()
                                     || layer->styles().hasEnabled();
        if (needMaterialize) {
            // 复用层局部复合缓存：平移只改 offset，不再每帧整层模糊
            const Layer::CompositeRaster raster = layer->ensureCompositeRaster();
            if (!raster.image.isNull()) {
                blendTileOnto(dst, raster.image,
                              layerOx + raster.originDx,
                              layerOy + raster.originDy,
                              opacity, modeOp, area,
                              mask, layerOx, layerOy);
            }
            continue;
        }

        layer->tiles().forEachAllocatedTile(
            [&](int, int, const QImage &tile, const QRect &bounds) {
                const QRect docBounds = bounds.translated(layerOx, layerOy);
                if (!docBounds.intersects(area))
                    return;
                blendTileOnto(dst, tile,
                              bounds.x() + layerOx,
                              bounds.y() + layerOy,
                              opacity, modeOp, area,
                              mask, layerOx, layerOy);
            });
    }

    return true;
}

} // namespace Ps
