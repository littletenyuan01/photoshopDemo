/**
 * sourceop.h — 图像源 / 图源算子（engine/graph 层）。
 *
 * 无输入；按请求区域返回源图子块。对照 GEGL buffer source / gegl:load。
 */
#ifndef ENGINE_GRAPH_SOURCEOP_H
#define ENGINE_GRAPH_SOURCEOP_H

#include "graphop.h"
#include "graphsource.h"

#include <QImage>
#include <memory>

namespace Ps {

/**
 * 图像源算子：0 输入，按 @p out 区域返回源图子块。
 * 供滤镜链把「待滤镜的源图」接入 DAG（Stage 2）。
 */
class ImageSourceOp : public GraphOp
{
public:
    explicit ImageSourceOp(const QImage &source) : m_source(source) {}

    int inputCount() const override { return 0; }

    QVector<QRect> requiredInputs(const QRect &out) const override
    {
        Q_UNUSED(out);
        return {};
    }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        Q_UNUSED(ins);
        if (m_source.isNull())
            return {};
        return m_source.copy(out.intersected(m_source.rect()));
    }

private:
    QImage m_source; ///< 源图（只读）
};

/**
 * 图源算子：0 输入，把 GraphSource 提供的区域接入 DAG（Stage 3）。
 * 持有 shared_ptr 保证源对象生命周期覆盖求值期。
 */
class SourceOp : public GraphOp
{
public:
    explicit SourceOp(std::shared_ptr<const GraphSource> source)
        : m_source(std::move(source)) {}

    int inputCount() const override { return 0; }

    QVector<QRect> requiredInputs(const QRect &out) const override
    {
        Q_UNUSED(out);
        return {};
    }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        Q_UNUSED(ins);
        return m_source ? m_source->read(out) : QImage();
    }

private:
    std::shared_ptr<const GraphSource> m_source;
};

} // namespace Ps

#endif // ENGINE_GRAPH_SOURCEOP_H
