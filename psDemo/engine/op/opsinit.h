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
