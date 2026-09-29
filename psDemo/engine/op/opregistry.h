/**
 * opregistry.h — 缓冲算子注册表（engine/op 层）。
 *
 * 启动期 opsInit 写入工厂与 requiredPads；OpRunner 查表调度。
 * 对照 GIMP gimp_operations_init / GEGL 类型注册。
 */
#ifndef ENGINE_OP_OPREGISTRY_H
#define ENGINE_OP_OPREGISTRY_H

#include "opname.h"
#include "oppad.h"

#include <QVector>
#include <functional>
#include <memory>

namespace Ps {

class BufferOp;

/**
 * 缓冲算子注册项：主键为 OpName 枚举；机器名/标题见 opNameId / opNameTitle。
 * requiredPads 在 OpRunner 执行前校验（对照 GEGL 缺 pad 则无法 process）。
 */
struct OpRegistration
{
    OpName name = OpName::Count;
    QVector<OpPad> requiredPads;                              ///< OpRunner 执行前校验的 pad
    std::function<std::unique_ptr<BufferOp>()> create;        ///< 工厂 lambda（非实例）
};

class OpRegistry
{
public:
    /** 注册一项；同名覆盖。 */
    static void add(OpRegistration reg);
    /** 按 OpName 查注册项；未注册返回 nullptr。 */
    static const OpRegistration *find(OpName name);
};

} // namespace Ps

#endif // ENGINE_OP_OPREGISTRY_H
