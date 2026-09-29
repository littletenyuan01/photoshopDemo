#include "oprunner.h"

#include "bufferop.h"
#include "opregistry.h"

#include <memory>
#include <unordered_map>

namespace Ps {

namespace {

bool padSatisfied(OpPad pad, const OpContext &ctx)
{
    switch (pad) {
    case OpPad::Tiles:
        return ctx.tiles != nullptr;
    case OpPad::Selection:
        return true;
    }
    return false;
}

bool resolveDependencies(const OpRegistration &reg, const OpContext &ctx)
{
    for (OpPad pad : reg.requiredPads) {
        if (!padSatisfied(pad, ctx))
            return false;
    }
    return true;
}

/** 常驻实例表：键 = OpName。单线程（UI 线程）访问，无锁。
 *  不用 QHash：隐式共享要求值可拷贝，unique_ptr 不行。 */
std::unordered_map<int, std::unique_ptr<BufferOp>> &instanceTable()
{
    static std::unordered_map<int, std::unique_ptr<BufferOp>> t;
    return t;
}

} // namespace

BufferOp *OpRunner::instance(OpName name)
{
    const int key = static_cast<int>(name);
    auto &table = instanceTable();
    const auto it = table.find(key);
    if (it != table.end())
        return it->second.get();

    const OpRegistration *reg = OpRegistry::find(name);
    if (!reg || !reg->create)
        return nullptr;

    std::unique_ptr<BufferOp> op = reg->create();
    if (!op)
        return nullptr;

    BufferOp *raw = op.get();
    table.emplace(key, std::move(op));
    return raw;
}

QRect OpRunner::run(OpName name, OpContext &ctx, const Configure &configure)
{
    const OpRegistration *reg = OpRegistry::find(name);
    if (!reg || !resolveDependencies(*reg, ctx))
        return {};

    BufferOp *op = instance(name);
    if (!op)
        return {};

    if (configure)
        configure(*op);

    QRect dirty;
    if (op->prepare(ctx))
        dirty = op->process(ctx);
    op->finish(ctx);
    return dirty;
}

} // namespace Ps
