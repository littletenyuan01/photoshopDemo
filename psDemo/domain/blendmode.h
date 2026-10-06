/**
 * blendmode.h — 图层混合模式枚举：PS 27 种，顺序 = PS 分组顺序（domain 层）。
 *
 * 枚举序与图层面板 `.ui` 项、`io/psdio.cpp` 四字符码须三处同步；合成期由 engine/blend 消费。
 */
#ifndef BLENDMODE_H
#define BLENDMODE_H

namespace Ps {

/**
 * 图层混合模式：**PS 的 27 种**，顺序 = PS 图层面板下拉里的分组顺序。
 *
 * 【对照 GIMP】公式全部来自 `gimp-master/app/operations/layer-modes/gimpoperationlayermode-blend.c`
 * （逐模式对应函数见 `engine/blend.cpp` 的 case 注释）。
 * GIMP 自己的枚举（`app/operations/operations-enums.h` 的 `GimpLayerMode`）是**历史追加顺序**，
 * 还带一整套 `*_LEGACY` 变体；这里按 PS 的展示顺序排，不复刻 GIMP 的枚举值。
 *
 * 【合成模型】自底向顶累积：
 * - `in`（本项目叫 backdrop / 下方已合成结果）—— GIMP 源码里的 `in`
 * - `layer`（当前层像素）—— GIMP 源码里的 `layer`
 * - Alpha 合成走 `gimp_operation_layer_mode_composite_union`（见 `engine/compositor.cpp`）
 *
 * ⚠️ **枚举序 = `layertreepanel.ui` 项顺序 = PS 分组顺序**（不含分隔线）。
 * 图层面板会在运行时插入分隔线，并用 `itemData` 存枚举值 —— **不要用 combo 下标当模式**。
 * 增删/重排模式时必须三处同步：本枚举、`layertreepanel.ui` 的项、`io/psdio.cpp` 的四字符码。
 * 工程文件存的是 `int(枚举)`。布局不兼容时改 `ProjectFormat`，不保证旧 `.pslite` 可开。
 * 增删/重排混合模式时同步：本枚举、`layertreepanel.ui`、`io/psdio.cpp`。
 */
enum class BlendMode {
    // —— 正常组 ——
    Normal = 0,          ///< 正常（GIMP LAYER_MODE_NORMAL）
    Dissolve = 1,        ///< 溶解（GIMP gimpoperationdissolve.c，逐像素随机阈值）

    // —— 变暗组 ——
    Darken = 2,          ///< 变暗（darken_only）
    Multiply = 3,        ///< 正片叠底（multiply）
    ColorBurn = 4,       ///< 颜色加深（burn）
    LinearBurn = 5,      ///< 线性加深（linear_burn）
    DarkerColor = 6,     ///< 深色（luma_darken_only：整像素按亮度取舍）

    // —— 变亮组 ——
    Lighten = 7,         ///< 变亮（lighten_only）
    Screen = 8,          ///< 滤色（screen）
    ColorDodge = 9,      ///< 颜色减淡（dodge）
    LinearDodge = 10,    ///< 线性减淡（添加）（addition）
    LighterColor = 11,   ///< 浅色（luma_lighten_only）

    // —— 对比组 ——
    Overlay = 12,        ///< 叠加（overlay）
    SoftLight = 13,      ///< 柔光（softlight，GIMP/Pegtop 式）
    HardLight = 14,      ///< 强光（hardlight）
    VividLight = 15,     ///< 亮光（vivid_light）
    LinearLight = 16,    ///< 线性光（linear_light）
    PinLight = 17,       ///< 点光（pin_light）
    HardMix = 18,        ///< 实色混合（hard_mix）

    // —— 反相组 ——
    Difference = 19,     ///< 差值（difference）
    Exclusion = 20,      ///< 排除（exclusion）
    Subtract = 21,       ///< 减去（subtract）
    Divide = 22,         ///< 划分（divide）

    // —— 分量组（不是逐通道算，取整像素的色相/饱和度/明度）——
    Hue = 23,            ///< 色相（hsv_hue）
    Saturation = 24,     ///< 饱和度（hsv_saturation）
    Color = 25,          ///< 颜色（hsl_color）
    Luminosity = 26,     ///< 明度（luminance）
};

/** 模式个数（= `.ui` 里模式项数，不含运行时插入的分隔线）。 */
inline constexpr int kBlendModeCount = 27;

/** 存档/枚举整数是否落在有效模式范围内（用于**拒绝**坏数据，而不是静默取 Normal）。 */
inline constexpr bool isValidBlendMode(int value)
{
    return value >= 0 && value < kBlendModeCount;
}

/** 枚举序数 → 模式；越界回落到 Normal（落盘读入请先用 isValidBlendMode 校验）。 */
inline BlendMode blendModeFromIndex(int index)
{
    return isValidBlendMode(index) ? static_cast<BlendMode>(index) : BlendMode::Normal;
}

/** 模式 → 枚举序数（0..26）；不是带分隔线的 combo 下标。 */
inline int blendModeToIndex(BlendMode mode)
{
    const int i = static_cast<int>(mode);
    return isValidBlendMode(i) ? i : static_cast<int>(BlendMode::Normal);
}

} // namespace Ps

#endif // BLENDMODE_H
