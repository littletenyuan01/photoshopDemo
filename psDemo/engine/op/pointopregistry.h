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
    std::function<std::unique_ptr<PointOp>()> create;
};

class PointOpRegistry
{
public:
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
