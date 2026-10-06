/**
 * layerstack.h — 自底向顶的图层列表，unique_ptr 独占所有权（domain 层）。
 *
 * 改栈方法 private + friend ImageDocument，入栈必经 addLayer 以挂 owner。
 */
#ifndef LAYERSTACK_H
#define LAYERSTACK_H

#include "layer.h"

#include <memory>
#include <vector>

namespace Ps {

class ImageDocument;

/**
 * 图层栈：自底向顶排列（index 0 = 最底层，最后绘制在最上面）。
 * 使用 std::vector<unique_ptr>：Qt6 的 QVector 对不可拷贝类型不友好。
 *
 * 【为什么改栈的方法是 private + friend】
 * `Layer` 需要 `owner` 回指 `ImageDocument` 才能广播属性信号。早先 `addLayer` 是
 * public，`createBlank` 与「打开图片」两处直接调用、**漏挂 owner**，导致改背景层的
 * 显隐/透明度/改名时属性信号不发、画布与面板**静默不同步**（有实测复现：
 * 背景层改显隐后 contentChanged 发出 0 次，而 addTransparentLayer 建的层是 1 次）。
 *
 * 现在只允许 `ImageDocument` 改栈，入栈必经 `ImageDocument::addLayer()`——
 * 那里是挂 owner 的唯一位置。调用方漏挂会**编译不过**，不再靠人记。
 */
class LayerStack
{
public:
    /** 图层个数。 */
    int count() const { return static_cast<int>(m_layers.size()); }
    bool isEmpty() const { return m_layers.empty(); }

    /**
     * 按栈下标取层（0 = 最底层）。
     * 只读访问；改属性请走 ImageDocument 的语义化 setter。
     */
    Layer *layerAt(int index);
    const Layer *layerAt(int index) const;

private:
    friend class ImageDocument;   // 只有文档能改栈（见类注释）

    /** 追加到栈顶（最上层），返回新层下标。 */
    int addLayer(std::unique_ptr<Layer> layer);
    /**
     * 插入到指定下标（0=最底 … count=栈顶之后）。
     * @return 实际插入下标；越界会钳到 [0, count]
     */
    int insertLayer(int index, std::unique_ptr<Layer> layer);
    /** 取出并移除；调用方获得所有权。越界返回 nullptr。 */
    std::unique_ptr<Layer> takeLayer(int index);
    /** 重新排序：from 层移动后落到下标 to（最终下标语义）。 */
    void moveLayer(int from, int to);

    /** 自底向顶；unique_ptr 独占每层所有权。 */
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace Ps

#endif // LAYERSTACK_H
