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
}

Layer *ImageDocument::activeLayer()
{
    return m_layers.layerAt(m_activeLayerIndex);
}

const Layer *ImageDocument::activeLayer() const
{
    return m_layers.layerAt(m_activeLayerIndex);
}

void ImageDocument::markDirty()
{
    m_dirty = true;
    emit documentChanged();
}

void ImageDocument::clearDirty()
{
    m_dirty = false;
}

int ImageDocument::addTransparentLayer(const QString &name)
{
    // 空名则自动编号；新层挂在栈顶（合成时最后画、视觉最靠上）
    const QString layerName = name.isEmpty()
                                  ? QStringLiteral("图层 %1").arg(m_layers.count() + 1)
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, m_width, m_height);
    const int index = m_layers.addLayer(std::move(layer));
    m_activeLayerIndex = index;
    m_dirty = true;
    emit structureChanged();
    emit activeLayerChanged(index);
    emit documentChanged();
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
    emit structureChanged();
    emit activeLayerChanged(m_activeLayerIndex);
    emit documentChanged();
    return true;
}

void ImageDocument::notifyLayerVisualChanged()
{
    // 不改层数/顺序，面板可不整表重建；画布必须重合成
    m_dirty = true;
    emit documentChanged();
}

void ImageDocument::notifyStructureChanged()
{
    // 供面板在「只改了栈顺序」等外部操作后统一通知
    m_dirty = true;
    emit structureChanged();
    emit documentChanged();
}

} // namespace Ps
