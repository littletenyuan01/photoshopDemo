/**
 * pathstack.h — 命名路径列表，unique_ptr 独占所有权（domain 层）。
 *
 * 改栈仅允许 ImageDocument（与 LayerStack 同一收口），避免漏挂信号。
 */
#ifndef DOMAIN_PATHSTACK_H
#define DOMAIN_PATHSTACK_H

#include "path.h"

#include <memory>
#include <vector>

namespace Ps {

class ImageDocument;

class PathStack
{
public:
    int count() const { return static_cast<int>(m_paths.size()); }
    bool isEmpty() const { return m_paths.empty(); }

    Path *pathAt(int index);
    const Path *pathAt(int index) const;

private:
    friend class ImageDocument;

    int addPath(std::unique_ptr<Path> path);
    std::unique_ptr<Path> takePath(int index);
    bool replacePath(int index, Path path);

    std::vector<std::unique_ptr<Path>> m_paths;
};

} // namespace Ps

#endif
