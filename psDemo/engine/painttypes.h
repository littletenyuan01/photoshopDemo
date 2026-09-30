/**
 * painttypes.h — 绘制共享类型（engine 层）。
 *
 * PaintMode / GradientType 供 tools、UI、PaintEngine、ops 共用，避免 UI 拉入算子头。
 */
#ifndef ENGINE_PAINTTYPES_H
#define ENGINE_PAINTTYPES_H

namespace Ps {

/**
 * 绘制侧共享类型（tools / UI / PaintEngine / ops 共用）。
 * 放在独立头文件，避免 UI 为拿一个枚举而拉入全部算子。
 */

/** 画笔 / 橡皮（对照 paint core 的 paint vs erase）。 */
enum class PaintMode {
    Paint,
    Erase,
};

/** 渐变形状（数值 = 选项栏 gradTypeCombo 顺序）。 */
enum class GradientType {
    Linear = 0,
    Radial,
    Angle,
    Reflected,
    Diamond,
};

/**
 * 聚焦工具模式（对照 GIMP Convolve / Smudge）。
 * Blur / Sharpen 走卷积；Smudge 沿笔画方向拖拽采样。
 */
enum class FocusMode {
    Blur = 0,
    Sharpen,
    Smudge,
};

/** 色调工具：减淡 / 海绵（对照 GIMP DodgeBurn / Sponge）。 */
enum class ToneMode {
    Dodge = 0, ///< 提亮
    Sponge,    ///< 提高饱和度
};

} // namespace Ps

#endif // ENGINE_PAINTTYPES_H
