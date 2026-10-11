/**
 * filterop.cpp — 滤镜算子工厂表 + 每线程常驻实例表（engine/op 层）。
 *
 * 两张表分工（对照 oprunner.cpp 的单线程实例表）：
 *  · 工厂表：`opsInit` 在主线程填一次，之后**只读** → 任何线程读都安全；
 *  · 实例表：**thread_local**，每个线程各自惰性创建并缓存自己的实例 → 无需加锁。
 *    这是本项目的算子线程协议（见 filterop.h 的说明与 docs/engine/operators.md）。
 */
#include "filterop.h"

#include <unordered_map>
#include <utility>

namespace Ps {

namespace {

std::unordered_map<int, FilterOpFactory> &factoryTable()
{
    static std::unordered_map<int, FilterOpFactory> t;
    return t;
}

/** 每线程一份：并行分块下同一算子会被多线程同时调用，共享实例状态会竞争。 */
std::unordered_map<int, std::unique_ptr<FilterOp>> &instanceTable()
{
    thread_local std::unordered_map<int, std::unique_ptr<FilterOp>> t;
    return t;
}

} // namespace

void registerFilterOp(OpName name, FilterOpFactory factory)
{
    factoryTable()[static_cast<int>(name)] = std::move(factory);
}

FilterOp *filterOpInstance(OpName name)
{
    const int key = static_cast<int>(name);

    auto &instances = instanceTable();
    const auto hit = instances.find(key);
    if (hit != instances.end())
        return hit->second.get();

    const auto fit = factoryTable().find(key);
    if (fit == factoryTable().end() || !fit->second)
        return nullptr;

    std::unique_ptr<FilterOp> op = fit->second();
    if (!op)
        return nullptr;

    FilterOp *raw = op.get();
    instances.emplace(key, std::move(op));
    return raw;
}

} // namespace Ps
