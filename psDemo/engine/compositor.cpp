#include "compositor.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"

#include <QRect>
#include <QtGlobal>

namespace Ps {

namespace {

/**
 * 预乘 Alpha 的 Normal 混合（Porter-Duff over）：
 *   out = src + dst * (1 - src.a)
 * src 位于文档坐标 (ox, oy)，尺寸为 tile 大小。
 */
void blendNormalPremultipliedAt(QImage &dst,
                                const QImage &src,
                                int ox,
                                int oy,
                                qreal opacity,
                                const QRect &dirty)
{
    if (opacity <= 0.0 || src.isNull())
        return;

    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirty.intersected(dst.rect()).intersected(tileRect);
    if (area.isEmpty())
        return;

    const int opacityQ = qBound(0, int(opacity * 255.0 + 0.5), 255);

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y - oy))
                        + (area.left() - ox);
        for (int x = 0; x < area.width(); ++x) {
            const QRgb sp = s[x];
            int sr = qRed(sp);
            int sg = qGreen(sp);
            int sb = qBlue(sp);
            int sa = qAlpha(sp);

            if (opacityQ != 255) {
                sr = (sr * opacityQ + 127) / 255;
                sg = (sg * opacityQ + 127) / 255;
                sb = (sb * opacityQ + 127) / 255;
                sa = (sa * opacityQ + 127) / 255;
            }

            if (sa == 0)
                continue;
            if (sa == 255) {
                d[x] = qRgba(sr, sg, sb, 255);
                continue;
            }

            const QRgb dp = d[x];
            const int dr = qRed(dp);
            const int dg = qGreen(dp);
            const int db = qBlue(dp);
            const int da = qAlpha(dp);
            const int inv = 255 - sa;

            const int or_ = sr + (dr * inv + 127) / 255;
            const int og = sg + (dg * inv + 127) / 255;
            const int ob = sb + (db * inv + 127) / 255;
            const int oa = sa + (da * inv + 127) / 255;
            d[x] = qRgba(or_, og, ob, oa);
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

    const QRect area = rect.intersected(result.rect());
    if (area.isEmpty())
        return result;

    const LayerStack &stack = doc.layers();
    for (int i = 0; i < stack.count(); ++i) {
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;
        // 无已分配瓦片 ≈ 全透明，跳过（对照 GIMP 跳过空 tile）
        if (!layer->hasPixelData())
            continue;

        layer->tiles().forEachAllocatedTile(
            [&](int, int, const QImage &tile, const QRect &bounds) {
                if (!bounds.intersects(area))
                    return;
                blendNormalPremultipliedAt(result, tile, bounds.x(), bounds.y(),
                                           layer->opacity(), area);
            });
    }

    return result;
}

} // namespace Ps
