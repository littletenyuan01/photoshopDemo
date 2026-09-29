#include "imagedocument.h"

#include <QImage>
#include <QPainter>
#include <QtGlobal>

#include <memory>

namespace Ps {

ImageDocument::ImageDocument(int width, int height, QObject *parent)
    : QObject(parent)
    , m_width(width)
    , m_height(height)
    , m_selection(width, height)
{
}

std::unique_ptr<ImageDocument> ImageDocument::createBlank(int width, int height,
                                                          const QColor &background)
{
    auto doc = std::make_unique<ImageDocument>(width, height);
    auto bg = std::make_unique<Layer>(QStringLiteral("背景"), width, height);
    bg->fill(background);
    // 走 addLayer 而不是 m_layers.addLayer：那里是挂 owner 的唯一位置。
    // 早先直接调 m_layers.addLayer 漏挂 owner → 改背景层显隐/透明度时属性信号不发、
    // 画布与面板静默不同步（有实测复现：contentChanged 发 0 次）。
    const int index = doc->addLayer(std::move(bg));
    doc->m_activeLayerIndex = index;
    return doc;
}

int ImageDocument::addLayer(std::unique_ptr<Layer> layer)
{
    if (!layer)
        return -1;

    layer->setOwner(this);   // ← 唯一的 owner 挂载点：所有入栈都必须经此
    const int index = m_layers.addLayer(std::move(layer));

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit contentChanged();
    return index;
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

int ImageDocument::pickLayerAt(int docX, int docY) const
{
    // 【功能】自栈顶向下找第一个不透明度 > 0.25 的层（对照 gimp_image_pick_layer）
    constexpr qreal kPickThreshold = 0.25;
    for (int i = m_layers.count() - 1; i >= 0; --i) {
        const Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;
        if (layer->opacityAtDocumentPos(docX, docY) > kPickThreshold)
            return i;
    }
    return -1;
}

void ImageDocument::clearSelection()
{
    m_selection.clear();
    emit selectionChanged();
}

void ImageDocument::selectAll()
{
    m_selection.selectAll();
    emit selectionChanged();
}

void ImageDocument::invertSelection()
{
    m_selection.invert();
    emit selectionChanged();
}

void ImageDocument::selectRectangle(const QRect &rect, ChannelOp op)
{
    // 【功能】对照 gimp_channel_select_rectangle → 写入 image selection_mask
    m_selection.selectRectangle(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectEllipse(const QRect &rect, ChannelOp op)
{
    // 【功能】对照 gimp_channel_select_ellipse → 写入 image selection_mask
    m_selection.selectEllipse(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectLayerAlpha(int layerIndex, ChannelOp op)
{
    // 【功能】对照 gimp_channel_select_alpha / layers-alpha-to-selection
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer)
        return;

    QImage pixels;
    if (layer->hasPixelData())
        pixels = layer->materialize();
    // 无瓦片：空图 → 全透明 alpha → Replace 清空选区
    m_selection.selectFromLayerAlpha(pixels, layer->offsetX(), layer->offsetY(), op);
    emit selectionChanged();
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

void ImageDocument::replaceSelectionMask(const QImage &mask)
{
    m_selection.replaceFromImage(mask);
    emit selectionChanged();
}

void ImageDocument::markDirty(const QRect &rect)
{
    if (rect.isEmpty())
        return;

    m_dirty = true;
    // 累计脏区：与既有并集合并（首次赋值时直接取 rect）
    m_dirtyRect = m_dirtyRect.isNull() ? rect : m_dirtyRect.united(rect);

    // 像素可能变了：失效活动层内容包围盒（变换控件用）
    if (Layer *layer = activeLayer())
        layer->invalidateContentBounds();

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

void ImageDocument::translateLayer(int index, int dx, int dy)
{
    // 【功能】移动工具：只改 offset，脏区覆盖旧位∪新位（对照 gimp_layer_real_translate 两次 update）
    if (dx == 0 && dy == 0)
        return;
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;

    const QRect docRect(0, 0, m_width, m_height);
    const QRect oldBounds = layer->boundsInDocument().intersected(docRect);
    layer->translate(dx, dy);
    const QRect newBounds = layer->boundsInDocument().intersected(docRect);
    // 属性面板将来可读 X/Y；像素刷新靠 markDirty → contentChanged
    emit layerPropertiesChanged(index);
    markDirty(oldBounds.united(newBounds));
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
    // 【功能】栈顶新建透明层并设为活动层（图层面板「新建」/ Ctrl+Shift+N）
    // 空名则自动编号；新层挂在栈顶（合成时最后画、视觉最靠上）
    const QString layerName = name.isEmpty()
                                  ? QStringLiteral("图层 %1").arg(m_layers.count() + 1)
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, m_width, m_height);
    // addLayer 内挂 owner、发 structureChanged + contentChanged（挂载点只有这一处）
    const int index = addLayer(std::move(layer));
    if (index < 0)
        return -1;

    // 走 setter：统一发 activeLayerChanged + contentChanged，面板/属性栏一起更新
    setActiveLayerIndex(index);
    return index;
}

int ImageDocument::duplicateLayer(int index)
{
    // 【功能】对照 GIMP layers_duplicate_cmd_callback → gimp_item_duplicate + gimp_image_add_layer
    // 【放置】PS：副本出现在源层上方 → 本栈更高下标 = 面板更靠上
    Layer *src = m_layers.layerAt(index);
    if (!src)
        return -1;

    const QString copyName = src->name().isEmpty()
                                 ? QStringLiteral("图层 副本")
                                 : src->name() + QStringLiteral(" 副本");
    auto copy = std::make_unique<Layer>(copyName, src->width(), src->height());
    if (src->hasPixelData())
        copy->replaceFromImage(src->materialize());
    // owner 尚未挂：属性 setter 不会广播，安全
    copy->setVisible(src->isVisible());
    copy->setOpacity(src->opacity());
    copy->setBlendMode(src->blendMode());
    copy->setOffsetSilent(src->offsetX(), src->offsetY());

    copy->setOwner(this);
    const int newIndex = m_layers.insertLayer(index + 1, std::move(copy));

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    // 活动层改到副本（setActiveLayerIndex 会再发 contentChanged；此处先发 structure）
    m_activeLayerIndex = newIndex;
    emit activeLayerChanged(newIndex);
    emit contentChanged();
    return newIndex;
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

void ImageDocument::scaleImage(int newWidth, int newHeight)
{
    // 【功能】PS「图像大小」+ 重新采样：每层像素缩放到新尺寸（内容跟着变大/变小）
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    if (newWidth == m_width && newHeight == m_height)
        return;

    for (int i = 0; i < m_layers.count(); ++i) {
        Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;
        // 瓦片 → 整图 → 平滑缩放 → 再拆回瓦片
        QImage src = layer->materialize();
        if (src.isNull()) {
            src = QImage(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
            src.fill(0);
        }
        QImage scaled = src.scaled(newWidth, newHeight, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
        if (scaled.format() != QImage::Format_ARGB32_Premultiplied)
            scaled = scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        layer->replaceFromImage(scaled);
        layer->setOffsetSilent(qRound(layer->offsetX() * qreal(newWidth) / m_width),
                               qRound(layer->offsetY() * qreal(newHeight) / m_height));
    }

    m_selection.scale(newWidth, newHeight);
    m_width = newWidth;
    m_height = newHeight;
    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    // 尺寸变了：面板缩略图/状态栏都要跟着重建
    emit structureChanged();
    emit selectionChanged();
    emit contentChanged();
}

void ImageDocument::resizeCanvas(int newWidth, int newHeight,
                                 int anchorRow, int anchorCol,
                                 const QColor &extensionColor)
{
    // 【功能】PS「画布大小」：改工作台尺寸，图层内容不缩放，只按锚点平移（加边或裁边）
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    anchorRow = qBound(0, anchorRow, 2);
    anchorCol = qBound(0, anchorCol, 2);
    if (newWidth == m_width && newHeight == m_height)
        return;

    // 锚点决定旧内容落点：左/上 = 0，中 = 一半，右/下 = 全部差额
    const int offsetX = (newWidth - m_width) * anchorCol / 2;
    const int offsetY = (newHeight - m_height) * anchorRow / 2;

    for (int i = 0; i < m_layers.count(); ++i) {
        Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;

        QImage src = layer->materialize();
        if (src.isNull()) {
            src = QImage(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
            src.fill(0);
        }

        QImage neu(newWidth, newHeight, QImage::Format_ARGB32_Premultiplied);
        // 底层用扩展色填空白；其余层保持透明（对齐 PS 分层画布扩展）
        if (i == 0 && extensionColor.alpha() > 0)
            neu.fill(extensionColor);
        else
            neu.fill(Qt::transparent);

        QPainter painter(&neu);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // 画布锚点 + 层原有 offset；写入新缓冲后 offset 归零（像素已烘焙进文档坐标）
        painter.drawImage(offsetX + layer->offsetX(), offsetY + layer->offsetY(), src);
        painter.end();

        if (neu.format() != QImage::Format_ARGB32_Premultiplied)
            neu = neu.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        layer->replaceFromImage(neu);
        layer->setOffsetSilent(0, 0);
    }

    m_selection.resizeCanvas(newWidth, newHeight, offsetX, offsetY);
    m_width = newWidth;
    m_height = newHeight;
    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit selectionChanged();
    emit contentChanged();
}

} // namespace Ps
