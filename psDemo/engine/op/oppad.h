/**
 * oppad.h — 算子依赖 pad 枚举（engine/op 层）。
 *
 * OpRegistry 声明、OpRunner 校验；对照 GEGL input/output/aux pad 名。
 */
#ifndef ENGINE_OP_OPPAD_H
#define ENGINE_OP_OPPAD_H

namespace Ps {

/**
 * 算子依赖 pad（对照 GEGL 的 input / output / aux 等 pad 名）。
 * Demo 里扁平映射到 OpContext 字段，由 OpRegistry 声明、OpRunner 校验。
 */
enum class OpPad {
    Tiles,     ///< 读写目标 TileBuffer*（对照 input+output buffer）
    Selection, ///< 可选选区裁剪（对照 mask / aux）
};

} // namespace Ps

#endif // ENGINE_OP_OPPAD_H
