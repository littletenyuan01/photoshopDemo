/**
 * opsinit.h — 内置算子注册入口（engine/op 层）。
 *
 * main 在 QApplication 之后调用一次；对照 GIMP gimp_operations_init。
 */
#ifndef ENGINE_OP_OPSINIT_H
#define ENGINE_OP_OPSINIT_H

namespace Ps {

/**
 * 注册内置算子（对照 gimp_operations_init）。
 * 在 main 里 QApplication 之后、进入 UI 之前调用一次。
 */
void opsInit();

} // namespace Ps

#endif // ENGINE_OP_OPSINIT_H
