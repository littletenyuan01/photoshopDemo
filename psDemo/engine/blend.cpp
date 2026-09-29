#include "blend.h"

#include <QtGlobal>

#include <cmath>

namespace Ps {
namespace Blend {
namespace {

constexpr float kEps = 1e-6f;              ///< 对照 GIMP blend 文件里的 `EPSILON`
constexpr float kSafeDivMin = kEps;
constexpr float kSafeDivMax = 1.0f / kEps; ///< 对照 GIMP `SAFE_DIV_MAX`

/**
 * 对照 GIMP `safe_div()`：`|a| <= EPSILON` 直接返回 0；结果夹到 ±SAFE_DIV_MAX。
 * 除零（`b == 0`）时浮点会给出 ±inf，被夹成 ±SAFE_DIV_MAX —— 与 GIMP 同样靠
 * 「先算出界值、最后写像素时再夹」处理，而不是在中间偷偷改成 0。
 */
inline float safeDiv(float a, float b)
{
    if (!(std::fabs(a) > kSafeDivMin))
        return 0.0f;
    const float r = a / b;
    // NaN（0/0 已被上面拦掉，这里只可能是 inf/inf）也回落成 0，避免污染整块瓦片
    if (std::isnan(r))
        return 0.0f;
    return qBound(-kSafeDivMax, r, kSafeDivMax);
}

/** sRGB 空间的 RGB→亮度系数（对照 GIMP `babl_space_get_rgb_luminance` 对 sRGB 的取值）。 */
constexpr float kLumR = 0.2126f;
constexpr float kLumG = 0.7152f;
constexpr float kLumB = 0.0722f;

inline float luma(const float c[3])
{
    return c[0] * kLumR + c[1] * kLumG + c[2] * kLumB;
}

inline float min3(const float c[3])
{
    return qMin(c[0], qMin(c[1], c[2]));
}

inline float max3(const float c[3])
{
    return qMax(c[0], qMax(c[1], c[2]));
}

inline void copy3(const float src[3], float dst[3])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/** 需要「三通道一起看」的模式（分量组 + 深色/浅色）。 */
inline bool usesWholePixel(BlendMode mode)
{
    switch (mode) {
    case BlendMode::DarkerColor:
    case BlendMode::LighterColor:
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
        return true;
    default:
        return false;
    }
}

/**
 * 分量组 / 深浅色：整像素一起算，不逐通道独立。
 * 算法逐行对照 GIMP 的同名函数（见每个 case 的注释）。
 */
void blendWholePixel(BlendMode mode, const float in[3], const float layer[3], float comp[3])
{
    switch (mode) {
    case BlendMode::DarkerColor:
        // gimp_operation_layer_mode_blend_luma_darken_only：比较整像素亮度，背板更暗（含相等）就留背板
        copy3(luma(in) <= luma(layer) ? in : layer, comp);
        return;

    case BlendMode::LighterColor:
        // gimp_operation_layer_mode_blend_luma_lighten_only
        copy3(luma(in) >= luma(layer) ? in : layer, comp);
        return;

    case BlendMode::Hue: {
        // gimp_operation_layer_mode_blend_hsv_hue：取上层的**色相**，饱和度/明度仍按背板的相对关系
        const float srcMin = min3(layer);
        const float srcMax = max3(layer);
        const float srcDelta = srcMax - srcMin;

        if (srcDelta > kEps) {
            const float destMin = min3(in);
            const float destMax = max3(in);
            const float destDelta = destMax - destMin;
            const float destS = destMax ? destDelta / destMax : 0.0f;

            const float ratio = destS * destMax / srcDelta;
            const float offset = destMax - srcMax * ratio;
            for (int c = 0; c < 3; ++c)
                comp[c] = layer[c] * ratio + offset;
        } else {
            // 上层是灰的（无色相）→ 没有色相可借，原样保留下层
            copy3(in, comp);
        }
        return;
    }

    case BlendMode::Saturation: {
        // gimp_operation_layer_mode_blend_hsv_saturation：取上层**饱和度**，色相/明度按背板
        const float destMin = min3(in);
        const float destMax = max3(in);
        const float destDelta = destMax - destMin;

        if (destDelta > kEps) {
            const float srcMin = min3(layer);
            const float srcMax = max3(layer);
            const float srcDelta = srcMax - srcMin;
            const float srcS = srcMax ? srcDelta / srcMax : 0.0f;

            const float ratio = srcS * destMax / destDelta;
            const float offset = (1.0f - ratio) * destMax;
            for (int c = 0; c < 3; ++c)
                comp[c] = in[c] * ratio + offset;
        } else {
            // 背板本身是灰的（无饱和度可调）→ 结果就是它的明度
            comp[0] = comp[1] = comp[2] = destMax;
        }
        return;
    }

    case BlendMode::Color: {
        // gimp_operation_layer_mode_blend_hsl_color：取上层色相+饱和度，保留背板明度
        const float destL = (min3(in) + max3(in)) / 2.0f;
        const float srcL = (min3(layer) + max3(layer)) / 2.0f;

        if (std::fabs(srcL) > kEps && std::fabs(1.0f - srcL) > kEps) {
            const bool destHigh = destL > 0.5f;
            const bool srcHigh = srcL > 0.5f;

            const float dL = qMin(destL, 1.0f - destL);
            const float sL = qMin(srcL, 1.0f - srcL);
            const float ratio = dL / sL;

            float offset = 0.0f;
            if (destHigh)
                offset += 1.0f - 2.0f * dL;
            if (srcHigh)
                offset += 2.0f * dL - ratio;

            for (int c = 0; c < 3; ++c)
                comp[c] = layer[c] * ratio + offset;
        } else {
            comp[0] = comp[1] = comp[2] = destL;
        }
        return;
    }

    case BlendMode::Luminosity: {
        // gimp_operation_layer_mode_blend_luminance：按 上层亮度/背板亮度 缩放背板
        const float ratio = safeDiv(luma(layer), luma(in));
        for (int c = 0; c < 3; ++c)
            comp[c] = in[c] * ratio;
        return;
    }

    default:
        return; // 其余模式逐通道处理
    }
}

/**
 * 逐通道合并 B(Cb, Cs)（直通 0..1）。
 * 每个 case 后面标了 GIMP 对应函数；公式逐行照抄，含各自的夹取位置。
 */
void blendPerChannel(BlendMode mode, float cb, float cs, float *out)
{
    float v = cs;
    switch (mode) {
    case BlendMode::Normal:
        // gimp_operation_layer_mode_blend_normal / composite_union：B = Cs
        v = cs;
        break;
    case BlendMode::Dissolve:
        // 溶解不走这里（Compositor 用 dissolveKeeps 决定取舍），等同 Normal
        v = cs;
        break;
    case BlendMode::Darken:
        v = qMin(cb, cs); // darken_only
        break;
    case BlendMode::Multiply:
        v = cb * cs;
        break;
    case BlendMode::ColorBurn:
        v = 1.0f - safeDiv(1.0f - cb, cs); // burn
        break;
    case BlendMode::LinearBurn:
        v = cb + cs - 1.0f;
        break;
    case BlendMode::Lighten:
        v = qMax(cb, cs); // lighten_only
        break;
    case BlendMode::Screen:
        v = 1.0f - (1.0f - cb) * (1.0f - cs);
        break;
    case BlendMode::ColorDodge:
        v = safeDiv(cb, 1.0f - cs); // dodge
        break;
    case BlendMode::LinearDodge:
        v = cb + cs; // addition
        break;
    case BlendMode::Overlay:
        // overlay：以**背板** cb 是否过半为分界
        v = cb < 0.5f ? 2.0f * cb * cs : 1.0f - 2.0f * (1.0f - cs) * (1.0f - cb);
        break;
    case BlendMode::SoftLight: {
        // softlight（GIMP/Pegtop 式）：比 PS 的经典柔光更平滑，注意不是 W3C 那条带 sqrt 的公式
        const float multiply = cb * cs;
        const float screen = 1.0f - (1.0f - cb) * (1.0f - cs);
        v = (1.0f - cb) * multiply + cb * screen;
        break;
    }
    case BlendMode::HardLight:
        // hardlight：以**上层** cs 是否过半为分界（= Overlay 交换两侧）
        if (cs > 0.5f)
            v = qMin(1.0f - (1.0f - cb) * (1.0f - (cs - 0.5f) * 2.0f), 1.0f);
        else
            v = qMin(cb * (cs * 2.0f), 1.0f);
        break;
    case BlendMode::VividLight:
        // vivid_light：上层 ≤0.5 走「颜色加深」，>0.5 走「颜色减淡」
        if (cs <= 0.5f)
            v = qMax(1.0f - safeDiv(1.0f - cb, 2.0f * cs), 0.0f);
        else
            v = qMin(safeDiv(cb, 2.0f * (1.0f - cs)), 1.0f);
        break;
    case BlendMode::LinearLight:
        // linear_light：分段写法与 GIMP 一致（两段代数上都是 cb + 2cs − 1）
        v = cs <= 0.5f ? cb + 2.0f * cs - 1.0f : cb + 2.0f * (cs - 0.5f);
        break;
    case BlendMode::PinLight:
        // pin_light
        v = cs > 0.5f ? qMax(cb, 2.0f * (cs - 0.5f)) : qMin(cb, 2.0f * cs);
        break;
    case BlendMode::HardMix:
        v = cb + cs < 1.0f ? 0.0f : 1.0f;
        break;
    case BlendMode::Difference:
        v = std::fabs(cb - cs);
        break;
    case BlendMode::Exclusion:
        v = 0.5f - 2.0f * (cb - 0.5f) * (cs - 0.5f);
        break;
    case BlendMode::Subtract:
        v = cb - cs;
        break;
    case BlendMode::Divide:
        v = safeDiv(cb, cs);
        break;

    // 不可达：这 6 个模式已在 blendWholePixel 里整像素算完（保留 case 是为了让 -Wswitch 保持穷尽，
    // 将来新增模式忘记实现会直接编译告警，而不是静默退化成 Normal）
    case BlendMode::DarkerColor:
    case BlendMode::LighterColor:
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
        v = 0.0f;
        break;
    }
    *out = v;
}

/**
 * 溶解的确定性哈希：把 (x, y) 打散成 [0, 255) 的整数。
 * 乘法取自 xxHash/murmur 常用奇数（0x9E3779B1 = 黄金比），再做两次 xor-shift 混合。
 */
inline quint32 hash2d(int x, int y)
{
    quint32 h = quint32(x) * 0x9E3779B1u;
    h ^= quint32(y) * 0x85EBCA77u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

} // namespace

void pixel(BlendMode mode, const int in[3], const int layer[3], float comp[3])
{
    float in01[3];
    float layer01[3];
    for (int c = 0; c < 3; ++c) {
        in01[c] = in[c] / 255.0f;
        layer01[c] = layer[c] / 255.0f;
    }

    if (usesWholePixel(mode)) {
        blendWholePixel(mode, in01, layer01, comp);
    } else {
        for (int c = 0; c < 3; ++c) {
            float v = 0.0f;
            blendPerChannel(mode, in01[c], layer01[c], &v);
            comp[c] = v;
        }
    }

    // 回到 0..255 尺度（**不夹**，越界值交给合成公式）
    for (int c = 0; c < 3; ++c)
        comp[c] *= 255.0f;
}

bool dissolveKeeps(int x, int y, int layerAlpha, qreal opacity)
{
    // 对照 GIMP gimpoperationdissolve.c：value = layer[alpha] * opacity * 255，
    // 随机数 >= value 就丢弃上层；等价于「随机数 < value 才保留」
    const qreal value = (layerAlpha / 255.0) * opacity * 255.0;
    if (value <= 0.0)
        return false;
    if (value >= 255.0)
        return true;

    const int r = int(hash2d(x, y) % 255u); // [0, 254]，与 g_rand_int_range(gr, 0, 255) 同区间
    return qreal(r) < value;
}

} // namespace Blend
} // namespace Ps
