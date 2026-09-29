#include "compositor.h"

#include "blend.h"
#include "domain/blendmode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"

#include <QRect>
#include <QtGlobal>

#include <cmath>

namespace Ps {

namespace {

inline void unpremultiply(int pr, int pg, int pb, int a, int *r, int *g, int *b)
{
    if (a <= 0) {
        *r = *g = *b = 0;
        return;
    }
    if (a >= 255) {
        *r = pr;
        *g = pg;
        *b = pb;
        return;
    }
    *r = qMin(255, (pr * 255 + a / 2) / a);
    *g = qMin(255, (pg * 255 + a / 2) / a);
    *b = qMin(255, (pb * 255 + a / 2) / a);
}

/**
 * 把源瓦片按混合模式画到 dst（= 下方已合成结果，预乘 ARGB32）。
 *
 * 【对照 GIMP】GIMP 把这件事拆成两个文件，这里一一对应：
 * - 逐模式的颜色合并 B(Cb, Cs)：`Engine::Blend::pixel` ⇔ `gimpoperationlayermode-blend.c`
 * - Alpha 合成：下面这段 ⇔ `gimpoperationlayermode-composite.c` 的 `..._composite_union`
 *
 * union 形式（`in` = 背板，`layer` = 当前层，`la` = 层 Alpha × 图层不透明度，`ia` = 背板 Alpha）：
 * ```
 * new_alpha = la + (1 - la) * ia
 * ratio     = la / new_alpha
 * out       = ratio * (ia * (comp - layer) + layer - in) + in
 * ```
 * 即 PDF 可分离混合的并集；Normal 时 B = Cs，退化为 Porter-Duff over。
 * 【注意】不是 CLIP_TO_BACKDROP —— 透明背板上仍能看见上层（更接近 PS/GIMP 的默认观感）。
 *
 * 【与 GIMP 的差异（有意，见 docs/layers/compositing.md）】
 * 1. 色彩空间：本项目整条管线是 8-bit sRGB（与 PS 一致）；GIMP 现代模式按
 *    `gimp-layer-modes.c` 的 `blend_space` 逐模式选线性/感知空间、并在线性空间合成，
 *    所以数值上不会与 GIMP 逐位一致。
 * 2. comp 不先夹取：越界值先参与合成、最后写像素时才夹（GIMP 同样如此；
 *    先夹会改变半透明下的结果，`docs/tech-notes.md` 有实测）。
 */
void blendTileOnto(QImage &dst,
                   const QImage &src,
                   int ox,
                   int oy,
                   qreal opacity,
                   BlendMode mode,
                   const QRect &dirty)
{
    if (opacity <= 0.0 || src.isNull())
        return;

    const QRect tileRect(ox, oy, src.width(), src.height());
    const QRect area = dirty.intersected(dst.rect()).intersected(tileRect);
    if (area.isEmpty())
        return;

    const float opacityF = float(opacity);
    const bool dissolving = (mode == BlendMode::Dissolve);

    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(y)) + area.left();
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(y - oy))
                            + (area.left() - ox);

        for (int x = 0; x < area.width(); ++x) {
            const QRgb sp = sline[x];
            const int sa = qAlpha(sp);
            if (sa == 0)
                continue;

            int sr = qRed(sp);
            int sg = qGreen(sp);
            int sb = qBlue(sp);
            // 源：预乘 → 直通（像素 Alpha 单独留着，图层不透明度在下面乘）
            unpremultiply(sr, sg, sb, sa, &sr, &sg, &sb);

            const QRgb dp = dline[x];
            const int da = qAlpha(dp);
            int dr = 0, dg = 0, db = 0;
            if (da > 0)
                unpremultiply(qRed(dp), qGreen(dp), qBlue(dp), da, &dr, &dg, &db);

            // la = 像素 Alpha × 图层不透明度
            float la = (sa / 255.0f) * opacityF;
            if (dissolving) {
                // 溶解：丢弃 = 背板原样（直接 continue）；保留 = 当作**完全不透明**的上层
                // （对照 gimpoperationdissolve.c：保留时 out[alpha] = 1.0，这正是「点状镂空」的观感）
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
                // 对照 composite_union 的 `in_alpha == 0` 分支：out = layer
                // （公式本身也退化成 layer，这里只是省掉一次混合计算）
                out[0] = cs[0];
                out[1] = cs[1];
                out[2] = cs[2];
            } else {
                const int backdrop[3] = { dr, dg, db };
                const int source[3] = { sr, sg, sb };
                float comp[3];
                // 溶解的取舍已在上面的 dissolveKeeps 决定，颜色按 Normal 合并
                Blend::pixel(dissolving ? BlendMode::Normal : mode, backdrop, source, comp);

                const float ratio = la / ar01;
                for (int c = 0; c < 3; ++c)
                    out[c] = ratio * (ia * (comp[c] / 255.0f - cs[c]) + cs[c] - cb[c]) + cb[c];
            }

            const int ar = qBound(0, int(ar01 * 255.0f + 0.5f), 255);
            if (ar <= 0) {
                dline[x] = 0;
                continue;
            }

            // 直通 → 预乘（存储格式是 ARGB32_Premultiplied）
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
    // 【功能】自底向顶：每层相对「下方已合成结果」做混合（对照 GIMP filter stack）
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
        if (!layer->hasPixelData())
            continue;

        const int layerOx = layer->offsetX();
        const int layerOy = layer->offsetY();
        const BlendMode mode = layer->blendMode();
        const qreal opacity = layer->opacity();

        layer->tiles().forEachAllocatedTile(
            [&](int, int, const QImage &tile, const QRect &bounds) {
                const QRect docBounds = bounds.translated(layerOx, layerOy);
                if (!docBounds.intersects(area))
                    return;
                blendTileOnto(result, tile,
                              bounds.x() + layerOx,
                              bounds.y() + layerOy,
                              opacity, mode, area);
            });
    }

    return result;
}

} // namespace Ps
