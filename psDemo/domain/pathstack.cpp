/**
 * pathstack.cpp — PathStack 实现。
 */
#include "pathstack.h"

namespace Ps {

Path *PathStack::pathAt(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_paths[size_t(index)].get();
}

const Path *PathStack::pathAt(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_paths[size_t(index)].get();
}

int PathStack::addPath(std::unique_ptr<Path> path)
{
    if (!path)
        return -1;
    m_paths.push_back(std::move(path));
    return count() - 1;
}

std::unique_ptr<Path> PathStack::takePath(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    auto it = m_paths.begin() + index;
    std::unique_ptr<Path> out = std::move(*it);
    m_paths.erase(it);
    return out;
}

bool PathStack::replacePath(int index, Path path)
{
    Path *p = pathAt(index);
    if (!p)
        return false;
    *p = std::move(path);
    return true;
}

} // namespace Ps
