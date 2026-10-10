/**
 * graphsource.h — 图源抽象（engine/graph 层）。
 *
 * 按区域读取像素的接口；SourceOp 把外部数据（图层栅格 / 蒙版）接入 DAG。
 * 对照 GEGL buffer source / gegl:load。
 */
#ifndef ENGINE_GRAPH_GRAPHSOURCE_H
#define ENGINE_GRAPH_GRAPHSOURCE_H

#include <QImage>
#include <QRect>

namespace Ps {

/**
 * 图源抽象：无输入节点的数据来源。
 * 具体实现（图层栅格、蒙版、纯色等）由调用方提供并持有；算子经 shared_ptr 引用。
 */
class GraphSource
{
public:
    virtual ~GraphSource() = default;

    /** 读 @p rect（文档坐标）区域，返回同尺寸图；区外透明（或黑）。 */
    virtual QImage read(const QRect &rect) const = 0;
};

} // namespace Ps

#endif // ENGINE_GRAPH_GRAPHSOURCE_H
