/**
 * filtereval.cpp — filtereval.h 实现（engine 层）。
 *
 * 按 OpName 分发；调整类在解预乘空间处理后再写回预乘像素。
 */
#include "filtereval.h"

#include "domain/filternode.h"
#include "engine/premul.h"

#include <QColor>
#include <QImage>
#include <QtMath>

#include <functional>

namespace Ps {
namespace FilterEval {

namespace {

void forEachPixel(QImage &image, const std::function<void(int *, int *, int *, int)> &fn)
{
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int r, g, b, a;
            Premul::unpremultiplyRgb(line[x], &r, &g, &b, &a);
            if (a <= 0)
                continue;
            fn(&r, &g, &b, a);
            line[x] = Premul::toPremultipliedRgb(QColor(r, g, b, a));
        }
    }
}

void applyBrightnessContrast(QImage &image, qreal brightness, qreal contrast)
{
    if (image.isNull() || (qFuzzyIsNull(brightness) && qFuzzyIsNull(contrast)))
        return;
    const qreal c = 1.0 + contrast;
    const qreal b = brightness;
    forEachPixel(image, [&](int *r, int *g, int *bl, int) {
        auto ch = [&](int v) {
            qreal n = v / 255.0;
            n = (n - 0.5) * c + 0.5 + b;
            return qBound(0, int(n * 255.0 + 0.5), 255);
        };
        *r = ch(*r);
        *g = ch(*g);
        *bl = ch(*bl);
    });
}

void applyInvert(QImage &image)
{
    forEachPixel(image, [](int *r, int *g, int *b, int) {
        *r = 255 - *r;
        *g = 255 - *g;
        *b = 255 - *b;
    });
}

void applyHueSaturation(QImage &image, qreal hueDeg, qreal satPct, qreal lightPct)
{
    if (qFuzzyIsNull(hueDeg) && qFuzzyIsNull(satPct) && qFuzzyIsNull(lightPct))
        return;
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        QColor c(*r, *g, *b);
        qreal h = c.hueF();
        qreal s = c.saturationF();
        qreal v = c.valueF();
        if (h < 0)
            h = 0;
        h = std::fmod(h + hueDeg / 360.0 + 1.0, 1.0);
        s = qBound(0.0, s + satPct / 100.0, 1.0);
        v = qBound(0.0, v + lightPct / 100.0, 1.0);
        c.setHsvF(h, s, v);
        *r = c.red();
        *g = c.green();
        *b = c.blue();
    });
}

void applyVibrance(QImage &image, qreal vibrance, qreal saturation)
{
    // 简化：低饱和像素多抬、高饱和少抬；再叠加全局饱和
    if (qFuzzyIsNull(vibrance) && qFuzzyIsNull(saturation))
        return;
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        QColor c(*r, *g, *b);
        qreal h = c.hueF();
        qreal s = c.saturationF();
        qreal v = c.valueF();
        if (h < 0)
            h = 0;
        const qreal boost = (vibrance / 100.0) * (1.0 - s);
        s = qBound(0.0, s + boost + saturation / 100.0, 1.0);
        c.setHsvF(h, s, v);
        *r = c.red();
        *g = c.green();
        *b = c.blue();
    });
}

void applyExposure(QImage &image, qreal exposure, qreal offset, qreal gamma)
{
    if (qFuzzyIsNull(exposure) && qFuzzyIsNull(offset) && qFuzzyCompare(gamma, 1.0))
        return;
    const qreal mul = qPow(2.0, exposure);
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        auto ch = [&](int v) {
            qreal n = v / 255.0;
            n = n * mul + offset;
            n = qBound(0.0, n, 1.0);
            if (!qFuzzyCompare(gamma, 1.0))
                n = qPow(n, 1.0 / gamma);
            return qBound(0, int(n * 255.0 + 0.5), 255);
        };
        *r = ch(*r);
        *g = ch(*g);
        *b = ch(*b);
    });
}

void applyLevels(QImage &image, qreal black, qreal white, qreal gamma)
{
    if (black <= 0.0 && white >= 255.0 && qFuzzyCompare(gamma, 1.0))
        return;
    const qreal span = qMax(1.0, white - black);
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        auto ch = [&](int v) {
            qreal n = (v - black) / span;
            n = qBound(0.0, n, 1.0);
            if (!qFuzzyCompare(gamma, 1.0))
                n = qPow(n, 1.0 / gamma);
            return qBound(0, int(n * 255.0 + 0.5), 255);
        };
        *r = ch(*r);
        *g = ch(*g);
        *b = ch(*b);
    });
}

void applyColorBalance(QImage &image, qreal cr, qreal mg, qreal yb)
{
    if (qFuzzyIsNull(cr) && qFuzzyIsNull(mg) && qFuzzyIsNull(yb))
        return;
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        *r = qBound(0, int(*r + cr * 2.55 + 0.5), 255);
        *g = qBound(0, int(*g + mg * 2.55 + 0.5), 255);
        *b = qBound(0, int(*b + yb * 2.55 + 0.5), 255);
    });
}

void applyPhotoFilter(QImage &image, qreal hue, qreal density)
{
    if (density <= 0.0)
        return;
    QColor tint;
    tint.setHsvF(qBound(0.0, hue / 360.0, 1.0), 1.0, 1.0);
    const qreal t = density / 100.0;
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        *r = qBound(0, int(*r * (1.0 - t) + tint.red() * t + 0.5), 255);
        *g = qBound(0, int(*g * (1.0 - t) + tint.green() * t + 0.5), 255);
        *b = qBound(0, int(*b * (1.0 - t) + tint.blue() * t + 0.5), 255);
    });
}

void applyBlackAndWhite(QImage &image, const FilterNode &node)
{
    // 简化：按 RGB 权重混合为灰（忽略分色相权重的完整 PS 模型）
    const qreal wr = qMax(0.0, node.bwReds());
    const qreal wg = qMax(0.0, node.bwGreens());
    const qreal wb = qMax(0.0, node.bwBlues());
    qreal sum = wr + wg + wb;
    if (sum < 1e-6)
        sum = 1.0;
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        const int y = qBound(0, int((*r * wr + *g * wg + *b * wb) / sum + 0.5), 255);
        *r = *g = *b = y;
    });
}

void applyPosterize(QImage &image, int levels)
{
    levels = qBound(2, levels, 255);
    const qreal step = 255.0 / (levels - 1);
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        auto ch = [&](int v) {
            const int idx = qBound(0, int(v / step + 0.5), levels - 1);
            return qBound(0, int(idx * step + 0.5), 255);
        };
        *r = ch(*r);
        *g = ch(*g);
        *b = ch(*b);
    });
}

void applyThreshold(QImage &image, int thr)
{
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        const int y = int(0.2126 * *r + 0.7152 * *g + 0.0722 * *b + 0.5);
        const int v = (y >= thr) ? 255 : 0;
        *r = *g = *b = v;
    });
}

void buildCurveLut(const int *ys, int *lut256)
{
    static const int xs[5] = {0, 64, 128, 192, 255};
    for (int i = 0; i < 256; ++i) {
        int seg = 0;
        while (seg < 3 && i > xs[seg + 1])
            ++seg;
        const int x0 = xs[seg];
        const int x1 = xs[seg + 1];
        const int y0 = ys[seg];
        const int y1 = ys[seg + 1];
        if (x1 == x0) {
            lut256[i] = y0;
        } else {
            const qreal t = qreal(i - x0) / qreal(x1 - x0);
            lut256[i] = qBound(0, int(y0 + (y1 - y0) * t + 0.5), 255);
        }
    }
}

void applyCurves(QImage &image, const FilterNode &node)
{
    const int ys[5] = {node.curveY0(), node.curveY1(), node.curveY2(),
                       node.curveY3(), node.curveY4()};
    bool identity = true;
    static const int idY[5] = {0, 64, 128, 192, 255};
    for (int i = 0; i < 5; ++i) {
        if (ys[i] != idY[i]) {
            identity = false;
            break;
        }
    }
    if (identity)
        return;
    int lut[256];
    buildCurveLut(ys, lut);
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        *r = lut[*r];
        *g = lut[*g];
        *b = lut[*b];
    });
}

void applyChannelMixer(QImage &image, const FilterNode &node)
{
    const qreal rr = node.mixRr() / 100.0;
    const qreal rg = node.mixRg() / 100.0;
    const qreal rb = node.mixRb() / 100.0;
    const qreal gr = node.mixGr() / 100.0;
    const qreal gg = node.mixGg() / 100.0;
    const qreal gb = node.mixGb() / 100.0;
    const qreal br = node.mixBr() / 100.0;
    const qreal bg = node.mixBg() / 100.0;
    const qreal bb = node.mixBb() / 100.0;
    const bool mono = node.mixMonochrome();
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        const qreal R = *r, G = *g, B = *b;
        int nr = qBound(0, int(R * rr + G * rg + B * rb + 0.5), 255);
        int ng = qBound(0, int(R * gr + G * gg + B * gb + 0.5), 255);
        int nb = qBound(0, int(R * br + G * bg + B * bb + 0.5), 255);
        if (mono) {
            const int y = qBound(0, int(0.2126 * nr + 0.7152 * ng + 0.0722 * nb + 0.5), 255);
            nr = ng = nb = y;
        }
        *r = nr;
        *g = ng;
        *b = nb;
    });
}

void buildLookupLut(int preset, int *lutR, int *lutG, int *lutB)
{
    for (int i = 0; i < 256; ++i) {
        qreal n = i / 255.0;
        qreal r = n, g = n, b = n;
        switch (preset) {
        case 1: // 暖
            r = qBound(0.0, n * 1.08 + 0.04, 1.0);
            g = qBound(0.0, n * 1.02, 1.0);
            b = qBound(0.0, n * 0.92, 1.0);
            break;
        case 2: // 冷
            r = qBound(0.0, n * 0.92, 1.0);
            g = qBound(0.0, n * 1.02, 1.0);
            b = qBound(0.0, n * 1.10 + 0.02, 1.0);
            break;
        case 3: // 高对比
            r = g = b = qBound(0.0, (n - 0.5) * 1.35 + 0.5, 1.0);
            break;
        case 0: // 褪色
        default:
            r = qBound(0.0, n * 0.85 + 0.08, 1.0);
            g = qBound(0.0, n * 0.88 + 0.06, 1.0);
            b = qBound(0.0, n * 0.82 + 0.10, 1.0);
            break;
        }
        lutR[i] = int(r * 255.0 + 0.5);
        lutG[i] = int(g * 255.0 + 0.5);
        lutB[i] = int(b * 255.0 + 0.5);
    }
}

void applyColorLookup(QImage &image, int preset)
{
    int lutR[256], lutG[256], lutB[256];
    buildLookupLut(preset, lutR, lutG, lutB);
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        *r = lutR[*r];
        *g = lutG[*g];
        *b = lutB[*b];
    });
}

void applyGradientMap(QImage &image, const FilterNode &node)
{
    const QColor a = QColor::fromRgba(node.gradientMapColorA());
    const QColor b = QColor::fromRgba(node.gradientMapColorB());
    const qreal tMix = node.gradientMapStrength() / 100.0;
    if (tMix <= 0.0)
        return;
    forEachPixel(image, [&](int *r, int *g, int *bl, int) {
        const qreal lum = (0.2126 * *r + 0.7152 * *g + 0.0722 * *bl) / 255.0;
        const int mr = int(a.red() + (b.red() - a.red()) * lum + 0.5);
        const int mg = int(a.green() + (b.green() - a.green()) * lum + 0.5);
        const int mb = int(a.blue() + (b.blue() - a.blue()) * lum + 0.5);
        *r = qBound(0, int(*r * (1.0 - tMix) + mr * tMix + 0.5), 255);
        *g = qBound(0, int(*g * (1.0 - tMix) + mg * tMix + 0.5), 255);
        *bl = qBound(0, int(*bl * (1.0 - tMix) + mb * tMix + 0.5), 255);
    });
}

qreal selectiveMask(int target, int r, int g, int b)
{
    const qreal R = r / 255.0, G = g / 255.0, B = b / 255.0;
    const qreal maxc = qMax(R, qMax(G, B));
    const qreal minc = qMin(R, qMin(G, B));
    const qreal lum = 0.2126 * R + 0.7152 * G + 0.0722 * B;
    const qreal sat = (maxc <= 1e-6) ? 0.0 : (maxc - minc) / maxc;
    switch (target) {
    case 0: return qBound(0.0, (R - qMax(G, B)) * sat * 2.0, 1.0); // 红
    case 1: return qBound(0.0, (qMin(R, G) - B) * sat * 2.0, 1.0); // 黄
    case 2: return qBound(0.0, (G - qMax(R, B)) * sat * 2.0, 1.0); // 绿
    case 3: return qBound(0.0, (qMin(G, B) - R) * sat * 2.0, 1.0); // 青
    case 4: return qBound(0.0, (B - qMax(R, G)) * sat * 2.0, 1.0); // 蓝
    case 5: return qBound(0.0, (qMin(R, B) - G) * sat * 2.0, 1.0); // 洋红
    case 6: return qBound(0.0, (lum - 0.7) / 0.3, 1.0);           // 白
    case 7: return qBound(0.0, 1.0 - qAbs(lum - 0.5) * 2.0, 1.0) * (1.0 - sat);
    case 8: return qBound(0.0, (0.3 - lum) / 0.3, 1.0);           // 黑
    default: return 0.0;
    }
}

void applySelectiveColor(QImage &image, const FilterNode &node)
{
    const qreal dC = node.selectiveCyan() / 100.0;
    const qreal dM = node.selectiveMagenta() / 100.0;
    const qreal dY = node.selectiveYellow() / 100.0;
    const qreal dK = node.selectiveBlack() / 100.0;
    if (qFuzzyIsNull(dC) && qFuzzyIsNull(dM) && qFuzzyIsNull(dY) && qFuzzyIsNull(dK))
        return;
    const int target = node.selectiveColorTarget();
    forEachPixel(image, [&](int *r, int *g, int *b, int) {
        const qreal w = selectiveMask(target, *r, *g, *b);
        if (w <= 0.0)
            return;
        qreal R = *r / 255.0, G = *g / 255.0, B = *b / 255.0;
        // 相对模式近似：按 CMY/K 偏移推 RGB
        R = qBound(0.0, R * (1.0 - w * dC) * (1.0 - w * dK), 1.0);
        G = qBound(0.0, G * (1.0 - w * dM) * (1.0 - w * dK), 1.0);
        B = qBound(0.0, B * (1.0 - w * dY) * (1.0 - w * dK), 1.0);
        *r = int(R * 255.0 + 0.5);
        *g = int(G * 255.0 + 0.5);
        *b = int(B * 255.0 + 0.5);
    });
}

} // namespace

void applyNode(QImage &image, const FilterNode &node)
{
    switch (node.op()) {
    case OpName::BrightnessContrast:
        applyBrightnessContrast(image, node.brightness(), node.contrast());
        break;
    case OpName::Invert:
        applyInvert(image);
        break;
    case OpName::HueSaturation:
        applyHueSaturation(image, node.hue(), node.saturation(), node.lightness());
        break;
    case OpName::Vibrance:
        applyVibrance(image, node.vibrance(), node.saturation());
        break;
    case OpName::Exposure:
        applyExposure(image, node.exposure(), node.exposureOffset(),
                      node.gammaCorrection());
        break;
    case OpName::Levels:
        applyLevels(image, node.levelsBlack(), node.levelsWhite(), node.levelsGamma());
        break;
    case OpName::ColorBalance:
        applyColorBalance(image, node.colorBalanceCR(), node.colorBalanceMG(),
                          node.colorBalanceYB());
        break;
    case OpName::PhotoFilter:
        applyPhotoFilter(image, node.photoFilterHue(), node.photoFilterDensity());
        break;
    case OpName::BlackAndWhite:
        applyBlackAndWhite(image, node);
        break;
    case OpName::Posterize:
        applyPosterize(image, node.posterizeLevels());
        break;
    case OpName::Threshold:
        applyThreshold(image, node.threshold());
        break;
    case OpName::Curves:
        applyCurves(image, node);
        break;
    case OpName::ChannelMixer:
        applyChannelMixer(image, node);
        break;
    case OpName::ColorLookup:
        applyColorLookup(image, node.colorLookupPreset());
        break;
    case OpName::GradientMap:
        applyGradientMap(image, node);
        break;
    case OpName::SelectiveColor:
        applySelectiveColor(image, node);
        break;
    default:
        break;
    }
}

} // namespace FilterEval
} // namespace Ps
