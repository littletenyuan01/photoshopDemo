/**
 * layerstyleeval.cpp — 投影 / 外发光 / 描边 / 颜色叠加求值。
 *
 * 投影实现对照 GIMP PSD→gegl:dropshadow（x/y/radius/grow-radius/color/opacity）：
 * 取 alpha → grow → blur → 偏移 → 着色，画在内容下方。
 */
#include "layerstyleeval.h"

#include "domain/layerstylestack.h"
#include "engine/premul.h"

#include <QVector>
#include <QtMath>
#include <cstring>

namespace Ps {

namespace {

QImage toPremul(const QImage &src)
{
    if (src.isNull())
        return {};
    if (src.format() == QImage::Format_ARGB32_Premultiplied)
        return src;
    return src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/** 提取直通 alpha（0..255）灰度图。 */
QImage extractAlpha(const QImage &premul)
{
    QImage a(premul.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < premul.height(); ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(premul.constScanLine(y));
        uchar *d = a.scanLine(y);
        for (int x = 0; x < premul.width(); ++x)
            d[x] = uchar(qAlpha(s[x]));
    }
    return a;
}

/** 圆形邻域最大值膨胀（grow-radius）。 */
QImage dilateAlpha(const QImage &alpha, int radius)
{
    if (radius <= 0 || alpha.isNull())
        return alpha;

    const int w = alpha.width();
    const int h = alpha.height();
    QImage out(w, h, QImage::Format_Grayscale8);
    out.fill(0);

    const int r2 = radius * radius;
    for (int y = 0; y < h; ++y) {
        uchar *dline = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int best = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sy = y + dy;
                if (sy < 0 || sy >= h)
                    continue;
                const uchar *sline = alpha.constScanLine(sy);
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > r2)
                        continue;
                    const int sx = x + dx;
                    if (sx < 0 || sx >= w)
                        continue;
                    best = qMax(best, int(sline[sx]));
                }
            }
            dline[x] = uchar(best);
        }
    }
    return out;
}

/** 可分离盒式模糊（多次近似高斯）。 */
QImage boxBlurAlpha(const QImage &alpha, int radius, int passes = 3)
{
    if (radius <= 0 || alpha.isNull())
        return alpha;

    const int w = alpha.width();
    const int h = alpha.height();
    QImage a = alpha;
    QImage b(w, h, QImage::Format_Grayscale8);

    auto blurPass = [&](const QImage &src, QImage &dst) {
        // 水平
        QImage tmp(w, h, QImage::Format_Grayscale8);
        const int diam = radius * 2 + 1;
        for (int y = 0; y < h; ++y) {
            const uchar *s = src.constScanLine(y);
            uchar *d = tmp.scanLine(y);
            int sum = 0;
            for (int i = -radius; i <= radius; ++i)
                sum += s[qBound(0, i, w - 1)];
            for (int x = 0; x < w; ++x) {
                d[x] = uchar(sum / diam);
                const int leave = x - radius;
                const int enter = x + radius + 1;
                sum -= s[qBound(0, leave, w - 1)];
                sum += s[qBound(0, enter, w - 1)];
            }
        }
        // 垂直
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int i = -radius; i <= radius; ++i)
                sum += tmp.constScanLine(qBound(0, i, h - 1))[x];
            for (int y = 0; y < h; ++y) {
                dst.scanLine(y)[x] = uchar(sum / diam);
                const int leave = y - radius;
                const int enter = y + radius + 1;
                sum -= tmp.constScanLine(qBound(0, leave, h - 1))[x];
                sum += tmp.constScanLine(qBound(0, enter, h - 1))[x];
            }
        }
    };

    for (int p = 0; p < passes; ++p) {
        blurPass(a, b);
        a.swap(b);
    }
    return a;
}

/** alpha 掩膜着色为预乘图（写到 dst 的 (ox,oy)）。 */
void stampColoredAlpha(QImage &dst, const QImage &alpha, int ox, int oy,
                       const QColor &color, qreal opacity)
{
    if (alpha.isNull() || dst.isNull() || opacity <= 0.0)
        return;

    const int cr = color.red();
    const int cg = color.green();
    const int cb = color.blue();
    const float op = float(opacity);

    for (int y = 0; y < alpha.height(); ++y) {
        const int dy = y + oy;
        if (dy < 0 || dy >= dst.height())
            continue;
        const uchar *aline = alpha.constScanLine(y);
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(dy));
        for (int x = 0; x < alpha.width(); ++x) {
            const int dx = x + ox;
            if (dx < 0 || dx >= dst.width())
                continue;
            const int sa = int(aline[x] * op + 0.5f);
            if (sa <= 0)
                continue;

            // Source-over onto dst（预乘）
            const int sr = (cr * sa + 127) / 255;
            const int sg = (cg * sa + 127) / 255;
            const int sb = (cb * sa + 127) / 255;

            const QRgb dp = dline[dx];
            const int da = qAlpha(dp);
            const int outA = sa + (da * (255 - sa) + 127) / 255;
            if (outA <= 0) {
                dline[dx] = 0;
                continue;
            }
            const int outR = sr + (qRed(dp) * (255 - sa) + 127) / 255;
            const int outG = sg + (qGreen(dp) * (255 - sa) + 127) / 255;
            const int outB = sb + (qBlue(dp) * (255 - sa) + 127) / 255;
            dline[dx] = qRgba(outR, outG, outB, outA);
        }
    }
}

void blitPremul(QImage &dst, const QImage &src, int ox, int oy)
{
    if (src.isNull())
        return;
    for (int y = 0; y < src.height(); ++y) {
        const int dy = y + oy;
        if (dy < 0 || dy >= dst.height())
            continue;
        const QRgb *sline = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *dline = reinterpret_cast<QRgb *>(dst.scanLine(dy));
        for (int x = 0; x < src.width(); ++x) {
            const int dx = x + ox;
            if (dx < 0 || dx >= dst.width())
                continue;
            const QRgb sp = sline[x];
            const int sa = qAlpha(sp);
            if (sa <= 0)
                continue;
            if (sa >= 255) {
                dline[dx] = sp;
                continue;
            }
            const QRgb dp = dline[dx];
            const int da = qAlpha(dp);
            const int outA = sa + (da * (255 - sa) + 127) / 255;
            const int outR = qRed(sp) + (qRed(dp) * (255 - sa) + 127) / 255;
            const int outG = qGreen(sp) + (qGreen(dp) * (255 - sa) + 127) / 255;
            const int outB = qBlue(sp) + (qBlue(dp) * (255 - sa) + 127) / 255;
            dline[dx] = qRgba(outR, outG, outB, outA);
        }
    }
}

QImage applyColorOverlay(const QImage &premul, const LayerStyleEffect &fx)
{
    if (!fx.isEnabled() || premul.isNull())
        return premul;

    QImage out = premul.copy();
    const int cr = fx.color().red();
    const int cg = fx.color().green();
    const int cb = fx.color().blue();
    const float t = float(fx.opacity());

    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            int r, g, b, a;
            Premul::unpremultiplyRgb(line[x], &r, &g, &b, &a);
            if (a <= 0)
                continue;
            const int nr = int(r * (1.0f - t) + cr * t + 0.5f);
            const int ng = int(g * (1.0f - t) + cg * t + 0.5f);
            const int nb = int(b * (1.0f - t) + cb * t + 0.5f);
            line[x] = Premul::toPremultipliedRgb(QColor(nr, ng, nb, a));
        }
    }
    return out;
}

/**
 * 内阴影 / 内发光：在内容 alpha 内侧着色。
 * 对照 GIMP/PSD → gegl:inner-glow（内阴影再加偏移）。
 */
void paintInnerLike(QImage &canvas, const QImage &alphaPad, int contentOx, int contentOy,
                    const LayerStyleEffect &fx)
{
    const int grow = int(qRound(fx.size() * (fx.spread() / 100.0)));
    const int blur = qMax(1, int(qRound(fx.size())));

    // 反相 alpha（形状外为实）→ 偏移 → 模糊 → 再与原 alpha 相乘 = 内侧阴影带
    QImage inv(alphaPad.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < alphaPad.height(); ++y) {
        const uchar *s = alphaPad.constScanLine(y);
        uchar *d = inv.scanLine(y);
        for (int x = 0; x < alphaPad.width(); ++x)
            d[x] = uchar(255 - s[x]);
    }

    if (grow > 0)
        inv = dilateAlpha(inv, grow);

    int ox = 0, oy = 0;
    fx.shadowOffset(&ox, &oy);

    QImage shifted = inv;
    if (ox != 0 || oy != 0) {
        shifted = QImage(inv.size(), QImage::Format_Grayscale8);
        shifted.fill(255); // 移出区域视为「外」
        for (int y = 0; y < inv.height(); ++y) {
            const int sy = y - oy;
            if (sy < 0 || sy >= inv.height())
                continue;
            const uchar *s = inv.constScanLine(sy);
            uchar *d = shifted.scanLine(y);
            for (int x = 0; x < inv.width(); ++x) {
                const int sx = x - ox;
                if (sx < 0 || sx >= inv.width())
                    continue;
                d[x] = s[sx];
            }
        }
    }

    QImage soft = boxBlurAlpha(shifted, blur);
    // 与内容 alpha 相乘 → 只留在形状内
    QImage mask(soft.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < soft.height(); ++y) {
        const uchar *a = alphaPad.constScanLine(y);
        const uchar *s = soft.constScanLine(y);
        uchar *d = mask.scanLine(y);
        for (int x = 0; x < soft.width(); ++x)
            d[x] = uchar((int(a[x]) * int(s[x]) + 127) / 255);
    }

    stampColoredAlpha(canvas, mask, contentOx, contentOy, fx.color(), fx.opacity());
}

void paintDropLike(QImage &canvas, const QImage &alphaSrc, int contentOx, int contentOy,
                   const LayerStyleEffect &fx, bool strokeOnly)
{
    int grow = int(qRound(fx.size() * (fx.spread() / 100.0)));
    int blur = int(qRound(fx.size()));
    if (strokeOnly) {
        grow = int(qRound(fx.size()));
        blur = 0;
    }

    QImage mask = alphaSrc;
    if (grow > 0)
        mask = dilateAlpha(mask, grow);
    if (blur > 0)
        mask = boxBlurAlpha(mask, blur);

    if (strokeOnly) {
        // 外侧描边 = 膨胀后减去原 alpha
        QImage ring = mask.copy();
        for (int y = 0; y < ring.height(); ++y) {
            uchar *d = ring.scanLine(y);
            const uchar *s = alphaSrc.constScanLine(y);
            for (int x = 0; x < ring.width(); ++x) {
                const int v = int(d[x]) - int(s[x]);
                d[x] = uchar(qMax(0, v));
            }
        }
        mask = ring;
    }

    int ox = 0, oy = 0;
    fx.shadowOffset(&ox, &oy);
    stampColoredAlpha(canvas, mask, contentOx + ox, contentOy + oy, fx.color(), fx.opacity());
}

} // namespace

StyledLayerResult LayerStyleEval::apply(const QImage &source, const LayerStyleStack &styles)
{
    StyledLayerResult result;
    QImage premul = toPremul(source);
    if (premul.isNull())
        return result;

    if (!styles.hasEnabled()) {
        result.image = premul;
        return result;
    }

    // 颜色叠加改内容本身，先做
    QImage content = premul;
    for (int i = 0; i < styles.count(); ++i) {
        const LayerStyleEffect &fx = styles.at(i);
        if (fx.isEnabled() && fx.kind() == LayerStyleKind::ColorOverlay)
            content = applyColorOverlay(content, fx);
    }

    const int pad = styles.maxPadding();
    const int cw = content.width();
    const int ch = content.height();
    QImage canvas(cw + pad * 2, ch + pad * 2, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    const int cox = pad;
    const int coy = pad;

    // 在带 padding 的 alpha 上做 grow/blur，避免边缘效果被裁切
    QImage alphaPad(cw + pad * 2, ch + pad * 2, QImage::Format_Grayscale8);
    alphaPad.fill(0);
    {
        const QImage alpha = extractAlpha(content);
        for (int y = 0; y < ch; ++y) {
            const uchar *s = alpha.constScanLine(y);
            uchar *d = alphaPad.scanLine(y + coy) + cox;
            std::memcpy(d, s, size_t(cw));
        }
    }

    auto paintKind = [&](LayerStyleKind kind) {
        for (int i = 0; i < styles.count(); ++i) {
            const LayerStyleEffect &fx = styles.at(i);
            if (!fx.isEnabled() || fx.kind() != kind)
                continue;
            const bool stroke = (kind == LayerStyleKind::Stroke);
            // stroke 需要原 alpha 做差；其余直接用 pad 图
            if (stroke) {
                QImage core(cw + pad * 2, ch + pad * 2, QImage::Format_Grayscale8);
                core.fill(0);
                const QImage alpha = extractAlpha(content);
                for (int y = 0; y < ch; ++y) {
                    const uchar *s = alpha.constScanLine(y);
                    uchar *d = core.scanLine(y + coy) + cox;
                    std::memcpy(d, s, size_t(cw));
                }
                paintDropLike(canvas, core, 0, 0, fx, true);
            } else {
                paintDropLike(canvas, alphaPad, 0, 0, fx, false);
            }
        }
    };
    paintKind(LayerStyleKind::DropShadow);
    paintKind(LayerStyleKind::OuterGlow);
    paintKind(LayerStyleKind::Stroke);

    blitPremul(canvas, content, cox, coy);

    // 内阴影 / 内发光画在内容之上、限制在 alpha 内
    auto paintInner = [&](LayerStyleKind kind) {
        for (int i = 0; i < styles.count(); ++i) {
            const LayerStyleEffect &fx = styles.at(i);
            if (!fx.isEnabled() || fx.kind() != kind)
                continue;
            paintInnerLike(canvas, alphaPad, 0, 0, fx);
        }
    };
    paintInner(LayerStyleKind::InnerShadow);
    paintInner(LayerStyleKind::InnerGlow);

    result.image = canvas;
    result.originDx = -pad;
    result.originDy = -pad;
    return result;
}

} // namespace Ps
