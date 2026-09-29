/**
 * imagedocument.cpp — ImageDocument 图层/选区/几何/撤销与脏区广播（domain 层）。
 */
#include "imagedocument.h"

#include "app/historystack.h"
#include "app/undoitem.h"
#include "domain/filternode.h"
#include "engine/op/opname.h"
#include "engine/paintengine.h"

#include <QImage>
#include <QPainter>
#include <QtGlobal>

#include <memory>
#include <utility>
#include <vector>

namespace Ps {

/** 构造空文档：初始化选区 mask 与 HistoryStack。 */
ImageDocument::ImageDocument(int width, int height, QObject *parent)
    : QObject(parent)
    , m_width(width)
    , m_height(height)
    , m_selection(width, height)
    , m_history(std::make_unique<HistoryStack>(this))
{
}

ImageDocument::~ImageDocument() = default;

ImageDocument::HistorySuppress::HistorySuppress(ImageDocument &doc)
    : m_doc(doc)
{
    ++m_doc.m_historySuppress;
}

ImageDocument::HistorySuppress::~HistorySuppress()
{
    --m_doc.m_historySuppress;
}

bool ImageDocument::shouldRecordHistory() const
{
    return m_historySuppress == 0 && m_history && !m_history->isApplying();
}

std::unique_ptr<Layer> ImageDocument::takeLayerForUndo(int index)
{
    return m_layers.takeLayer(index);
}

void ImageDocument::insertLayerForUndo(int index, std::unique_ptr<Layer> layer)
{
    if (!layer)
        return;
    layer->setOwner(this);
    m_layers.insertLayer(index, std::move(layer));
}

void ImageDocument::undo()
{
    if (m_history)
        m_history->undo(*this);
}

void ImageDocument::redo()
{
    if (m_history)
        m_history->redo(*this);
}

void ImageDocument::pushLayerPixelsUndo(int layerIndex, const QString &label)
{
    if (!shouldRecordHistory())
        return;
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer)
        return;
    m_history->push(std::make_unique<LayerPixelsUndo>(
        layerIndex, layer->materialize(), label));
}

void ImageDocument::pushLayerOffsetUndo(int layerIndex)
{
    pushLayerPropUndo(layerIndex, tr("移动图层"));
}

void ImageDocument::pushLayerPropUndo(int index, const QString &label)
{
    if (!shouldRecordHistory())
        return;
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    m_history->push(std::make_unique<LayerPropUndo>(
        index, captureLayerProps(*layer), label));
}

void ImageDocument::pushDocumentGeomUndo(const QString &label)
{
    if (!shouldRecordHistory())
        return;
    std::vector<DocumentGeomUndo::LayerState> layers;
    layers.reserve(size_t(m_layers.count()));
    for (int i = 0; i < m_layers.count(); ++i) {
        Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;
        DocumentGeomUndo::LayerState st;
        st.props = captureLayerProps(*layer);
        st.pixels = layer->materialize();
        st.width = layer->width();
        st.height = layer->height();
        layers.push_back(std::move(st));
    }
    m_history->push(std::make_unique<DocumentGeomUndo>(
        m_width, m_height, m_selection.mask().copy(),
        std::move(layers), m_activeLayerIndex, label));
}

std::unique_ptr<ImageDocument> ImageDocument::createBlank(int width, int height,
                                                          const QColor &background)
{
    auto doc = std::make_unique<ImageDocument>(width, height);
    HistorySuppress suppress(*doc);
    auto bg = std::make_unique<Layer>(QStringLiteral("背景"), width, height);
    bg->fill(background);
    const int index = doc->addLayer(std::move(bg));
    doc->m_activeLayerIndex = index;
    return doc;
}

/** 入栈唯一入口：挂 owner、结构撤销、广播 structureChanged。 */
int ImageDocument::addLayer(std::unique_ptr<Layer> layer)
{
    if (!layer)
        return -1;

    layer->setOwner(this);
    const int index = m_layers.addLayer(std::move(layer));

    if (shouldRecordHistory()) {
        m_history->push(LayerStructureUndo::forAdded(index, tr("新建图层")));
    }

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit contentChanged();
    return index;
}

void ImageDocument::setActiveLayerIndex(int index)
{
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

bool ImageDocument::clearActiveLayerPixels()
{
    Layer *layer = activeLayer();
    if (!layer || !layer->isVisible())
        return false;

    const int ox = layer->offsetX();
    const int oy = layer->offsetY();
    if (!m_selection.isEmpty()
        && m_selection.bounds().intersected(layer->boundsInDocument()).isEmpty())
        return false;

    pushLayerPixelsUndo(m_activeLayerIndex, tr("清除"));

    PaintSelectionClip clip;
    clip.selection = m_selection.isEmpty() ? nullptr : &m_selection;
    clip.layerOffsetX = ox;
    clip.layerOffsetY = oy;
    const QRect dirtyLocal = PaintEngine::solidFill(layer->tiles(), Qt::transparent, clip);
    if (dirtyLocal.isEmpty())
        return false;

    markDirty(dirtyLocal.translated(ox, oy));
    return true;
}

bool ImageDocument::fillActiveLayer(const QColor &color)
{
    Layer *layer = activeLayer();
    if (!layer || !layer->isVisible())
        return false;

    const int ox = layer->offsetX();
    const int oy = layer->offsetY();
    if (!m_selection.isEmpty()
        && m_selection.bounds().intersected(layer->boundsInDocument()).isEmpty())
        return false;

    pushLayerPixelsUndo(m_activeLayerIndex, tr("填充"));

    PaintSelectionClip clip;
    clip.selection = m_selection.isEmpty() ? nullptr : &m_selection;
    clip.layerOffsetX = ox;
    clip.layerOffsetY = oy;
    const QRect dirtyLocal = PaintEngine::solidFill(layer->tiles(), color, clip);
    if (dirtyLocal.isEmpty())
        return false;

    markDirty(dirtyLocal.translated(ox, oy));
    return true;
}

int ImageDocument::addBrightnessContrastFilter(qreal brightness, qreal contrast)
{
    Layer *layer = activeLayer();
    if (!layer || !layer->isVisible())
        return -1;

    FilterNode node(OpName::BrightnessContrast);
    node.setBrightness(brightness);
    node.setContrast(contrast);
    const int index = layer->filters().append(node);
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return index;
}

bool ImageDocument::setLayerFilterEnabled(int layerIndex, int filterIndex, bool enabled)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || !layer->filters().setEnabled(filterIndex, enabled))
        return false;
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return true;
}

bool ImageDocument::removeLayerFilter(int layerIndex, int filterIndex)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || !layer->filters().removeAt(filterIndex))
        return false;
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return true;
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
    m_selection.selectRectangle(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectEllipse(const QRect &rect, ChannelOp op)
{
    m_selection.selectEllipse(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectPolygon(const QPolygonF &points, ChannelOp op)
{
    // 经 OpRunner → SelectPolygonOp（对照 gimp_channel_select_polygon）
    PaintEngine::selectPolygon(m_selection, points, op);
    emit selectionChanged();
}

void ImageDocument::selectLayerAlpha(int layerIndex, ChannelOp op)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer)
        return;

    QImage pixels;
    if (layer->hasPixelData())
        pixels = layer->materialize();
    m_selection.selectFromLayerAlpha(pixels, layer->offsetX(), layer->offsetY(), op);
    emit selectionChanged();
}

int ImageDocument::indexOfLayer(const Layer *layer) const
{
    if (!layer)
        return -1;
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

/** 累计脏区并使活动层内容包围盒缓存失效。 */
void ImageDocument::markDirty(const QRect &rect)
{
    if (rect.isEmpty())
        return;

    m_dirty = true;
    m_dirtyRect = m_dirtyRect.isNull() ? rect : m_dirtyRect.united(rect);

    if (Layer *layer = activeLayer())
        layer->invalidateContentBounds();

    emit pixelsChanged(rect);
    emit contentChanged();
}

void ImageDocument::markDirty()
{
    markDirty(QRect(0, 0, m_width, m_height));
}

void ImageDocument::setLayerVisible(int index, bool visible)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || layer->isVisible() == visible)
        return;
    pushLayerPropUndo(index, tr("图层可见性"));
    layer->setVisible(visible);
}

void ImageDocument::setLayerOpacity(int index, qreal opacity)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;
    const qreal clamped = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(layer->opacity(), clamped))
        return;
    pushLayerPropUndo(index, tr("图层不透明度"));
    layer->setOpacity(clamped);
}

void ImageDocument::setLayerName(int index, const QString &name)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || layer->name() == name)
        return;
    pushLayerPropUndo(index, tr("图层名称"));
    layer->setName(name);
}

void ImageDocument::pushLayerPropertiesUndo(int index, const QString &label)
{
    pushLayerPropUndo(index, label);
}

void ImageDocument::setLayerBlendMode(int index, BlendMode mode, bool recordHistory)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || layer->blendMode() == mode)
        return;
    if (recordHistory)
        pushLayerPropUndo(index, tr("图层混合模式"));
    layer->setBlendMode(mode);
}

void ImageDocument::translateLayer(int index, int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return;
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;

    const QRect docRect(0, 0, m_width, m_height);
    const QRect oldBounds = layer->boundsInDocument().intersected(docRect);
    layer->translate(dx, dy);
    const QRect newBounds = layer->boundsInDocument().intersected(docRect);
    emit layerPropertiesChanged(index);
    markDirty(oldBounds.united(newBounds));
}

void ImageDocument::notifyLayerPropertiesChanged(const Layer &layer)
{
    const int index = indexOfLayer(&layer);
    if (index < 0)
        return;

    const QRect docRect(0, 0, m_width, m_height);
    // Phase 7：属性变更只脏内容区，避免整图画布重投影
    QRect dirty = layer.contentBoundsInDocument().intersected(docRect);
    if (dirty.isEmpty())
        dirty = layer.boundsInDocument().intersected(docRect);
    if (!dirty.isEmpty()) {
        m_dirty = true;
        m_dirtyRect = m_dirtyRect.isNull() ? dirty : m_dirtyRect.united(dirty);
    }

    emit layerPropertiesChanged(index);
    emit contentChanged();
}

void ImageDocument::notifyLayerLabelChanged(const Layer &layer)
{
    const int index = indexOfLayer(&layer);
    if (index < 0)
        return;
    emit layerPropertiesChanged(index);
}

int ImageDocument::addTransparentLayer(const QString &name)
{
    const QString layerName = name.isEmpty()
                                  ? QStringLiteral("图层 %1").arg(m_layers.count() + 1)
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, m_width, m_height);
    const int index = addLayer(std::move(layer));
    if (index < 0)
        return -1;

    setActiveLayerIndex(index);
    return index;
}

/** 深拷贝像素、属性与滤镜栈，插入源层上方。 */
int ImageDocument::duplicateLayer(int index)
{
    Layer *src = m_layers.layerAt(index);
    if (!src)
        return -1;

    const QString copyName = src->name().isEmpty()
                                 ? QStringLiteral("图层 副本")
                                 : src->name() + QStringLiteral(" 副本");
    auto copy = std::make_unique<Layer>(copyName, src->width(), src->height());
    if (src->hasPixelData())
        copy->replaceFromImage(src->materialize());
    copy->setVisible(src->isVisible());
    copy->setOpacity(src->opacity());
    copy->setBlendMode(src->blendMode());
    copy->setOffsetSilent(src->offsetX(), src->offsetY());
    for (int i = 0; i < src->filters().count(); ++i)
        copy->filters().append(src->filters().at(i));

    copy->setOwner(this);
    const int newIndex = m_layers.insertLayer(index + 1, std::move(copy));

    if (shouldRecordHistory()) {
        m_history->push(LayerStructureUndo::forAdded(newIndex, tr("复制图层")));
    }

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    m_activeLayerIndex = newIndex;
    emit activeLayerChanged(newIndex);
    emit contentChanged();
    return newIndex;
}

bool ImageDocument::removeLayer(int index)
{
    if (m_layers.count() <= 1)
        return false;
    if (index < 0 || index >= m_layers.count())
        return false;

    auto taken = m_layers.takeLayer(index);
    if (shouldRecordHistory() && taken) {
        // 撤销删除时恢复该层并选中它
        m_history->push(LayerStructureUndo::forRemoved(
            index, std::move(taken), index, tr("删除图层")));
    }

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

/** 图像大小：各层 Smooth 重采样，选区最近邻缩放。 */
void ImageDocument::scaleImage(int newWidth, int newHeight)
{
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    if (newWidth == m_width && newHeight == m_height)
        return;

    pushDocumentGeomUndo(tr("图像大小"));

    for (int i = 0; i < m_layers.count(); ++i) {
        Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;
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
    emit structureChanged();
    emit selectionChanged();
    emit contentChanged();
}

/** 画布大小：按锚点偏移贴入各层，背景层可填扩展色。 */
void ImageDocument::resizeCanvas(int newWidth, int newHeight,
                                 int anchorRow, int anchorCol,
                                 const QColor &extensionColor)
{
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    anchorRow = qBound(0, anchorRow, 2);
    anchorCol = qBound(0, anchorCol, 2);
    if (newWidth == m_width && newHeight == m_height)
        return;

    pushDocumentGeomUndo(tr("画布大小"));

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
        if (i == 0 && extensionColor.alpha() > 0)
            neu.fill(extensionColor);
        else
            neu.fill(Qt::transparent);

        QPainter painter(&neu);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
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
