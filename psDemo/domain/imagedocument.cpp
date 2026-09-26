#include "imagedocument.h"

#include <memory>

namespace Ps {

ImageDocument::ImageDocument(int width, int height, QObject *parent)
    : QObject(parent)
    , m_width(width)
    , m_height(height)
{
}

std::unique_ptr<ImageDocument> ImageDocument::createBlank(int width, int height,
                                                          const QColor &background)
{
    auto doc = std::make_unique<ImageDocument>(width, height);
    auto bg = std::make_unique<Layer>(QStringLiteral("背景"), width, height);
    bg->fill(background);
    const int index = doc->m_layers.addLayer(std::move(bg));
    doc->m_activeLayerIndex = index;
    return doc;
}

void ImageDocument::setActiveLayerIndex(int index)
{
    // 允许 -1（无选中）；拒绝越界
    if (index < -1 || index >= m_layers.count())
        return;
    if (m_activeLayerIndex == index)
        return;
    m_activeLayerIndex = index;
    emit activeLayerChanged(index);
    emit contentChanged();
}

Layer *ImageDocument::activeLayer()
{
    return m_layers.layerAt(m_activeLayerIndex);
}

const Layer *ImageDocument::activeLayer() const
{
    return m_layers.layerAt(m_activeLayerIndex);
}

int ImageDocument::indexOfLayer(const Layer *layer) const
{
    if (!layer)
        return -1;
    // 层数通常个位数～几十，线性查找足够；不引入额外索引带来的失效风险
    for (int i = 0; i < m_layers.count(); ++i) {
        if (m_layers.layerAt(i) == layer)
            return i;
    }
    return -1;
}

void ImageDocument::clearDirty()
{
    m_dirty = false;
    m_dirtyRect = QRect();
}

void ImageDocument::markDirty(const QRect &rect)
{
    if (rect.isEmpty())
        return;

    m_dirty = true;
    // 累计脏区：与既有并集合并（首次赋值时直接取 rect）
    m_dirtyRect = m_dirtyRect.isNull() ? rect : m_dirtyRect.united(rect);

    emit pixelsChanged(rect);
    emit contentChanged();
}

void ImageDocument::markDirty()
{
    markDirty(QRect(0, 0, m_width, m_height));
}

// —— 语义化 setter ——

void ImageDocument::setLayerVisible(int index, bool visible)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    // Layer::setVisible 内部会回调 notifyLayerPropertiesChanged，无需在此重复广播
    layer->setVisible(visible);
}

void ImageDocument::setLayerOpacity(int index, qreal opacity)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    layer->setOpacity(opacity);
}

void ImageDocument::setLayerName(int index, const QString &name)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    layer->setName(name);
}

void ImageDocument::setLayerBlendMode(int index, BlendMode mode)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    layer->setBlendMode(mode);
}

void ImageDocument::notifyLayerPropertiesChanged(const Layer &layer)
{
    const int index = indexOfLayer(&layer);
    if (index < 0)
        return;

    m_dirty = true;
    // 整图重合成（属性变更可能影响任意像素），但面板只需更新第 index 行
    m_dirtyRect = m_dirtyRect.isNull() ? QRect(0, 0, m_width, m_height)
                                       : m_dirtyRect.united(QRect(0, 0, m_width, m_height));

    emit layerPropertiesChanged(index);
    emit contentChanged();
}

// —— 结构操作 ——

int ImageDocument::addTransparentLayer(const QString &name)
{
    // 空名则自动编号；新层挂在栈顶（合成时最后画、视觉最靠上）
    const QString layerName = name.isEmpty()
                                  ? QStringLiteral("图层 %1").arg(m_layers.count() + 1)
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, m_width, m_height);
    layer->setOwner(this); // 入栈即建立回指，之后属性 setter 可自动广播
    const int index = m_layers.addLayer(std::move(layer));
    m_activeLayerIndex = index;
    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit activeLayerChanged(index);
    emit contentChanged();
    return index;
}

bool ImageDocument::removeLayer(int index)
{
    // 至少保留一层，避免空文档无合成目标
    if (m_layers.count() <= 1)
        return false;
    if (index < 0 || index >= m_layers.count())
        return false;

    m_layers.takeLayer(index);

    // 删除后夹紧活动层下标，避免悬空
    if (m_activeLayerIndex >= m_layers.count())
        m_activeLayerIndex = m_layers.count() - 1;
    else if (m_activeLayerIndex > index)
        --m_activeLayerIndex;

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit activeLayerChanged(m_activeLayerIndex);
    emit contentChanged();
    return true;
}

} // namespace Ps
