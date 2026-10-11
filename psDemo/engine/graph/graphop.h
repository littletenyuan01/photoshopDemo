/**
 * graphop.h — 图算子接口（engine/graph 层）。
 *
 * 纯函数算子：无副作用，给定输出区域产出对应图像；ROI 经 requiredInputs 反向传播。
 * 对照 GEGL GeglOperation 的 prepare / process / get_bounding_box 语义壳。
 */
#ifndef ENGINE_GRAPH_GRAPHOP_H
#define ENGINE_GRAPH_GRAPHOP_H

#include <QImage>
#include <QRect>
#include <QVector>

namespace Ps {

/**
 * 图算子（对照 GeglOperation）。
 *
 * 【无副作用】process 只读输入、产出新图，不碰外部状态（算子参数只读）。
 * 【ROI】requiredInputs 声明「为产出 out 区域需要哪些输入区域」：
 *   点算子恒等（输入区 = 输出区）；面积算子（模糊等）外扩；为后续面积/变换算子留扩展点。
 *
 * 【坐标约定】输入/输出区域在同一文档坐标空间；当前算子均为逐像素对齐，
 *   输入区 = 输出区。变换类算子（位移/缩放）可打破对齐，暂不做。
 */
class GraphOp
{
public:
    virtual ~GraphOp() = default;

    /** 输入口个数（0 = 源节点，无输入）。 */
    virtual int inputCount() const = 0;

    /** 为产出 @p out 区域，各输入口所需区域（下标 0..inputCount-1）。 */
    virtual QVector<QRect> requiredInputs(const QRect &out) const = 0;

    /**
     * 求值：@p ins 覆盖 requiredInputs(out) 各区域；返回覆盖 @p out 的输出图。
     * 不得修改 @p ins。
     */
    virtual QImage process(const QVector<QImage> &ins, const QRect &out) const = 0;

    /**
     * 是否可**就地**处理：输入缓冲可被本算子直接改写（逐像素算子成立）。
     *
     * 求值器只在「该输入与本节点区域一致、且只被本节点消费」时才走就地路径，
     * 故对 DAG 里的其它消费者没有影响。
     */
    virtual bool canProcessInPlace() const { return false; }

    /**
     * 就地求值（可选）：@p ins 的元素可被直接改写（调用方保证其独占）。
     * 仅当 canProcessInPlace() 为真时被调用；默认退化为 process()。
     */
    virtual QImage processInPlace(QVector<QImage> &&ins, const QRect &out) const
    {
        return process(ins, out);
    }
};

} // namespace Ps

#endif // ENGINE_GRAPH_GRAPHOP_H
