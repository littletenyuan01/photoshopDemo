#include "layerstack.h"

namespace Ps {

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
    m_layers.push_back(std::move(layer));
    return count() - 1;
}

std::unique_ptr<Layer> LayerStack::takeLayer(int index)
{
    if (index < 0 || index >= count())
        return nullptr;
    auto it = m_layers.begin() + index;
    std::unique_ptr<Layer> layer = std::move(*it);
    m_layers.erase(it);
    return layer;
}

void LayerStack::moveLayer(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return;
    // 先取出再插入，保持 unique_ptr 所有权连续
    auto layer = std::move(m_layers[static_cast<size_t>(from)]);
    m_layers.erase(m_layers.begin() + from);
    m_layers.insert(m_layers.begin() + to, std::move(layer));
}

} // namespace Ps
