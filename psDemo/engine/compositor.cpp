#include "compositor.h"

#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
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
                   const QRect &dirty)
{
    if (opacity <= 0.0 || src.isNull() || !modeOp)
        return;

    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirty.intersected(dst.rect()).intersected(tileRect);
    if (area.isEmpty())
        return;

    const float opacityF = float(opacity);
    const bool dissolving = (modeOp->mode() == BlendMode::Dissolve);

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

    const LayerStack &stack = doc.layers();
    for (int i = 0; i < stack.count(); ++i) {
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;
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

        // Phase 8：有启用滤镜时对临时 materialize 求值，不写回层瓦片
        if (layer->filters().hasEnabled()) {
            const QImage filtered = layer->filters().apply(layer->materialize());
            if (!filtered.isNull()) {
                blendTileOnto(dst, filtered, layerOx, layerOy, opacity, modeOp, area);
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
                              opacity, modeOp, area);
            });
    }

    return true;
}

} // namespace Ps
