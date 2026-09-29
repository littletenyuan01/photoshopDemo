/**
 * opregistry.cpp — opregistry.h 实现（engine/op 层）。
 *
 * 函数内静态 QHash 存注册项；启动期 opsInit 写入，运行时只读。
 */
#include "opregistry.h"

#include <QHash>

namespace Ps {

namespace {

QHash<int, OpRegistration> &table()
{
    static QHash<int, OpRegistration> t;
    return t;
}

} // namespace

void OpRegistry::add(OpRegistration reg)
{
    const int key = static_cast<int>(reg.name);
    table().insert(key, std::move(reg));
}

const OpRegistration *OpRegistry::find(OpName name)
{
    const auto it = table().constFind(static_cast<int>(name));
    if (it == table().constEnd())
        return nullptr;
    return &it.value();
}

} // namespace Ps
