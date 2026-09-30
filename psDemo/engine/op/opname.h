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
 * 新增算子：先在此加枚举项，再补 opNameId / opNameTitle，最后 opsInit 注册。
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
    LayerMode,              ///< 图层混合色（点算子）
    BrightnessContrast,     ///< 亮度/对比度滤镜
    Count                   ///< 哨兵，勿当作真实算子
};

/** 稳定机器名，如 "ps:flood-fill"（对照 gegl "name" key / gimp:flood）。 */
QString opNameId(OpName name);

/** 可读标题（对照 gegl "title"）。 */
QString opNameTitle(OpName name);

} // namespace Ps

#endif // ENGINE_OP_OPNAME_H
