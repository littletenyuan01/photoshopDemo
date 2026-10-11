/**
 * filterop.cpp — 滤镜算子工厂表 + 每线程常驻实例表（engine/op 层）。
 *
 * 两张表分工（对照 oprunner.cpp 的单线程实例表）：
 *  · 工厂表：`opsInit` 在主线程填一次，之后**只读** → 任何线程读都安全；
 *  · 实例表：**每线程一份**，各自惰性创建并缓存 → 无需加锁。
 *    这是本项目的算子线程协议（见 filterop.h 的说明与 docs/engine/operators.md）。
 *
 * 【实例表为什么是「堆上且故意不析构」】见 instanceTable() 上方注释 —— 退出崩溃的根因与修法。
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

/**
 * 每线程一份：并行分块下同一算子会被多线程同时调用，共享实例状态会竞争。
 *
 * 【为什么容器放堆上、且**故意不 delete**】
 * 算子实例可能持有 QImage（面积算子的临时缓冲）。若让容器作为 thread_local 对象在
 * **进程退出**时析构，TLS 回调可能在 Qt 静态数据（乃至 CRT）之后运行 —— 那时销毁 QImage 会崩：
 * 实测「点右上角退出软件」崩在 `run_dtor_list → ~unordered_map → 释放节点`（GDB 栈）。
 * 改成「thread_local 指针 + 堆上容器、永不 delete」后，TLS 对象变成 **trivially destructible**，
 * 退出时**根本不跑析构**，从根上避开析构顺序问题（对上面两种竞争假设都免疫）。
 *
 * 代价与边界：容器本身每线程泄漏一个（几百字节）；线程退出时其缓冲也不再回收 ——
 * 故 `BoxBlurFilterOp` 把跨调用保留的缓冲上限压到 128×128（见其 kMaxRetainedPixels），
 * 使「Qt 线程池闲置线程过期重建」这种场景下每线程最多多留 ~128 KB。
 */
std::unordered_map<int, std::unique_ptr<FilterOp>> &instanceTable()
{
    thread_local auto *t = new std::unordered_map<int, std::unique_ptr<FilterOp>>();
    return *t;
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
