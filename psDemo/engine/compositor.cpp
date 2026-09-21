#include "compositor.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"

#include <QRect>
#include <QtGlobal>

namespace Ps {

namespace {

/**
 * 预乘 Alpha 的 Normal 混合（Porter-Duff over）：
 *   out = src + dst * (1 - src.a)
 * 其中 src 已先按图层 opacity 缩放。
 * 整数运算带 +127 做四舍五入，减少色带。
 */
void blendNormalPremultiplied(QImage &dst, const QImage &src, qreal opacity, const QRect &rect)
{
    if (opacity <= 0.0)
        return;

    const QRect area = rect.intersected(dst.rect()).intersected(src.rect());
    if (area.isEmpty())
        return;

    const int opacityQ = qBound(0, int(opacity * 255.0 + 0.5), 255);

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y)) + area.left();
        for (int x = 0; x < area.width(); ++x) {
            const QRgb sp = s[x];
            int sr = qRed(sp);
            int sg = qGreen(sp);
            int sb = qBlue(sp);
            int sa = qAlpha(sp);

            // 图层不透明度：预乘空间下 RGB 与 A 同比例缩放
            if (opacityQ != 255) {
                sr = (sr * opacityQ + 127) / 255;
                sg = (sg * opacityQ + 127) / 255;
                sb = (sb * opacityQ + 127) / 255;
                sa = (sa * opacityQ + 127) / 255;
            }

            if (sa == 0)
                continue;
            if (sa == 255) {
                // 完全盖住：无需读 dst
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
    // 输出与文档同尺寸；未合成区域保持透明，画布用棋盘格衬底
    QImage result(doc.width(), doc.height(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    const QRect area = rect.intersected(result.rect());
    if (area.isEmpty())
        return result;

    const LayerStack &stack = doc.layers();
    // i=0 最底；后画的层在视觉上更靠上
    for (int i = 0; i < stack.count(); ++i) {
        const Layer *layer = stack.layerAt(i);
        if (!layer || !layer->isVisible() || layer->opacity() <= 0.0)
            continue;

        // v1 忽略 BlendMode，一律 Normal
        blendNormalPremultiplied(result, layer->pixels(), layer->opacity(), area);
    }

    return result;
}

} // namespace Ps
