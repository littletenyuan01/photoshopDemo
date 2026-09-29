/**
 * oprunner.h — 缓冲算子调度器（engine/op 层）。
 *
 * 查 OpRegistry → pad 校验 → 取常驻实例 → configure → prepare → process → finish。
 * 对照 GIMP gimp_layer_mode_get_operation 的 per-mode 实例缓存。
 */
#ifndef ENGINE_OP_OPRUNNER_H
#define ENGINE_OP_OPRUNNER_H

#include "opcontext.h"
#include "opname.h"

#include <QRect>
#include <functional>

namespace Ps {

class BufferOp;

/**
 * 算子调度器：按 OpName 枚举查表，显式 prepare → process → finish。
 *
 * 实例生命周期对照 GIMP `gimp_layer_mode_get_operation`（gimp-layer-modes.c 的
 * `if (!ops[mode])` 缓存）：**每个 OpName 常驻一个实例**，反复调度只重设参数。
 * 不再每次 `new`——画笔一笔要跑几十个 dab，合成更是逐瓦片调用。
 */
class OpRunner
{
public:
    using Configure = std::function<void(BufferOp &)>;

    /** 跑一次：查表 → pad 校验 → 取常驻实例 → configure → prepare → process → finish。 */
    static QRect run(OpName name, OpContext &ctx, const Configure &configure = {});

    /**
     * 常驻算子实例（首次调用创建，之后复用）；未注册返回 nullptr。
     *
     * 【复用的前提】调用方必须把该算子的全部参数写一遍——实例上的参数会跨次残留。
     * 单线程（UI 线程）访问，不加锁。
     */
    static BufferOp *instance(OpName name);
};

} // namespace Ps

#endif // ENGINE_OP_OPRUNNER_H
