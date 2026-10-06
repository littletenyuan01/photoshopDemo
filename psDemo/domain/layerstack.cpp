/**
 * layerstack.cpp — LayerStack 增删插取与重排（domain 层）。
 */
#include "layerstack.h"

namespace Ps {

/** 越界返回 nullptr。 */
Layer *LayerStack::layerAt(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_layers[static_cast<size_t>(index)].get();
}

const Layer *LayerStack::layerAt(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_layers[static_cast<size_t>(index)].get();
}

int LayerStack::addLayer(std::unique_ptr<Layer> layer)
{
    // 追加到栈顶（最大下标 = 视觉最上层）；owner 由 ImageDocument::addLayer 先挂好
    m_layers.push_back(std::move(layer));
    return count() - 1;
}

int LayerStack::insertLayer(int index, std::unique_ptr<Layer> layer)
{
    if (index < 0)
        index = 0;
    if (index > count())
        index = count();
    m_layers.insert(m_layers.begin() + index, std::move(layer));
    return index;
}

std::unique_ptr<Layer> LayerStack::takeLayer(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    // 取出所有权后 erase，调用方负责销毁或另存
    auto it = m_layers.begin() + index;
    std::unique_ptr<Layer> layer = std::move(*it);
    m_layers.erase(it);
    return layer;
}

void LayerStack::moveLayer(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return;
    // 最终下标语义：取出后插入到 to（对照 GIMP gimp_container_reorder）
    auto layer = std::move(m_layers[static_cast<size_t>(from)]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
}

} // namespace Ps
