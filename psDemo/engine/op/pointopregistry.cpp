#include "pointopregistry.h"

#include "pointop.h"

#include <QHash>
#include <memory>
#include <unordered_map>

namespace Ps {

namespace {

QHash<int, PointOpRegistration> &pointTable()
{
    static QHash<int, PointOpRegistration> t;
    return t;
}

/** 常驻实例表：键 = OpName。单线程（UI 线程）访问，无锁。
 *  不用 QHash：隐式共享要求值可拷贝，unique_ptr 不行。 */
std::unordered_map<int, std::unique_ptr<PointOp>> &instanceTable()
{
    static std::unordered_map<int, std::unique_ptr<PointOp>> t;
    return t;
}

} // namespace

void PointOpRegistry::add(PointOpRegistration reg)
{
    pointTable().insert(static_cast<int>(reg.name), std::move(reg));
}

const PointOpRegistration *PointOpRegistry::find(OpName name)
{
    const auto it = pointTable().constFind(static_cast<int>(name));
    if (it == pointTable().constEnd())
        return nullptr;
    return &it.value();
}

PointOp *PointOpRegistry::instance(OpName name)
{
    const int key = static_cast<int>(name);
    auto &table = instanceTable();
    const auto it = table.find(key);
    if (it != table.end())
        return it->second.get();

    const PointOpRegistration *reg = find(name);
    if (!reg || !reg->create)
        return nullptr;

    std::unique_ptr<PointOp> op = reg->create();
    if (!op)
        return nullptr;

    PointOp *raw = op.get();
    table.emplace(key, std::move(op));
    return raw;
}

} // namespace Ps
