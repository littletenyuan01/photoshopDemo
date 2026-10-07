/**
 * opname.h — 算子名枚举与 id/title 表（engine/op 层）。
 *
 * OpName 为注册表与调度器主键；对照 GIMP 层模式 → "gimp:…" 字符串映射。
 */
#ifndef ENGINE_OP_OPNAME_H
#define ENGINE_OP_OPNAME_H

#include <QString>

namespace Ps {

/**
 * 算子名枚举（中央定义，对照 GIMP 层模式用 `GimpLayerMode` 枚举、
 * 再映射到 `"gimp:…"` 字符串；GEGL 本体用字符串注册，本 Demo 用枚举做类型安全主键）。
 *
 * 新增算子：先在此加枚举项，再补 opNameId / opNameTitle，最后 opsInit 注册
 * （调整类滤镜走 FilterEval，不必进 BufferOp 注册表）。
 */
enum class OpName {
    StampDab = 0,           ///< 画笔/橡皮 dab
    FloodFill,              ///< 油漆桶洪泛
    Gradient,               ///< 渐变填充
    SolidFill,              ///< 实色/透明填充
    SelectPolygon,          ///< 多边形/套索写入选区 mask
    SelectFlood,            ///< 连通域/相似色写入选区（魔棒）
    CloneStampDab,          ///< 仿制图章 dab
    FocusDab,               ///< 模糊 / 锐化 / 涂抹 dab
    ToneDab,                ///< 减淡 / 海绵 dab
    ShapeFill,              ///< 形状填充 / 描边
    FreeTransform,          ///< 自由变换（四边形透视/仿射）
    LayerMode,              ///< 图层混合色（点算子）
    // —— 调整类（FilterEval / 调整图层；对照 PS 调整图层菜单）——
    BrightnessContrast,     ///< 亮度/对比度
    Levels,                 ///< 色阶
    Curves,                 ///< 曲线（UI 先占位）
    Exposure,               ///< 曝光度
    Vibrance,               ///< 自然饱和度
    HueSaturation,          ///< 色相/饱和度
    ColorBalance,           ///< 色彩平衡
    BlackAndWhite,          ///< 黑白
    PhotoFilter,            ///< 照片滤镜
    ChannelMixer,           ///< 通道混合器（求值后置）
    ColorLookup,            ///< 颜色查找（求值后置）
    Invert,                 ///< 反相
    Posterize,              ///< 色调分离
    Threshold,              ///< 阈值
    GradientMap,            ///< 渐变映射（求值后置）
    SelectiveColor,         ///< 可选颜色（求值后置）
    Count                   ///< 哨兵，勿当作真实算子
};

/** 稳定机器名，如 "ps:flood-fill"（对照 gegl "name" key / gimp:flood）。 */
QString opNameId(OpName name);

/** 可读标题（UI / 图层默认名用中文，与 PS 菜单对齐）。 */
QString opNameTitle(OpName name);

/** 是否为调整图层可用的滤镜算子（非像素绘制类）。 */
bool isAdjustmentOp(OpName name);

} // namespace Ps

#endif // ENGINE_OP_OPNAME_H
