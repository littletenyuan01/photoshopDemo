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
    QVector<OpPad> requiredPads;
    std::function<std::unique_ptr<BufferOp>()> create;
};

class OpRegistry
{
public:
    static void add(OpRegistration reg);
    static const OpRegistration *find(OpName name);
};

} // namespace Ps

#endif // ENGINE_OP_OPREGISTRY_H
