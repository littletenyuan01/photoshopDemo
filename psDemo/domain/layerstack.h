#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "layer.h"

#include <memory>
#include <vector>

namespace Ps {

/**
 * 图层栈：自底向顶排列（index 0 = 最底层，最后绘制在最上面）。
 * 使用 std::vector<unique_ptr>：Qt6 的 QVector 对不可拷贝类型不友好。
 */
class LayerStack
{
public:
    int count() const { return static_cast<int>(m_layers.size()); }
    bool isEmpty() const { return m_layers.empty(); }

    Layer *layerAt(int index);
    const Layer *layerAt(int index) const;

    /** 追加到栈顶（最上层），返回新层下标。 */
    int addLayer(std::unique_ptr<Layer> layer);
    /** 取出并移除；调用方获得所有权。越界返回 nullptr。 */
    std::unique_ptr<Layer> takeLayer(int index);

    void moveLayer(int from, int to);

private:
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace Ps

#endif // LAYERSTACK_H
