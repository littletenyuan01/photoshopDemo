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
    StampDab = 0,
    FloodFill,
    Gradient,
    SolidFill,
    LayerMode,
    BrightnessContrast,
    Count ///< 哨兵，勿当作真实算子
};

/** 稳定机器名，如 "ps:flood-fill"（对照 gegl "name" key / gimp:flood）。 */
QString opNameId(OpName name);

/** 可读标题（对照 gegl "title"）。 */
QString opNameTitle(OpName name);

} // namespace Ps

#endif // ENGINE_OP_OPNAME_H
