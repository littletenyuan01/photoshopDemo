/**
 * pointopregistry.h — 点算子注册表（engine/op 层）。
 *
 * Compositor 合成热路径按 OpName 取常驻 LayerModeOp；对照 gimp_layer_mode_get_operation。
 */
#ifndef ENGINE_OP_POINTOPREGISTRY_H
#define ENGINE_OP_POINTOPREGISTRY_H

#include "opname.h"

#include <functional>
#include <memory>

namespace Ps {

class PointOp;

struct PointOpRegistration
{
    OpName name = OpName::Count;
    std::function<std::unique_ptr<PointOp>()> create; ///< 工厂 lambda（非实例）
};

class PointOpRegistry
{
public:
    /** 注册一项；同名覆盖。 */
    static void add(PointOpRegistration reg);

    /** 注册项查询；未注册返回 nullptr。 */
    static const PointOpRegistration *find(OpName name);

    /**
     * 常驻实例（首次调用创建，之后复用）；未注册返回 nullptr。
     *
     * 合成热路径逐瓦片取算子，若每次 new 就是「每瓦片一次堆分配」。
     * 对照 GIMP `gimp_layer_mode_get_operation`：每个 mode 缓存一个可复用 op。
     * 单线程（UI 线程）访问，不加锁。
     */
    static PointOp *instance(OpName name);
};

} // namespace Ps

#endif // ENGINE_OP_POINTOPREGISTRY_H
