/**
 * undoitem.cpp — 各 UndoItem 子类 pop() 与快照辅助函数的实现（app 层）。
 */
#include "undoitem.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"

#include <utility>

namespace Ps {

LayerPropSnapshot captureLayerProps(const Layer &layer)
{
    LayerPropSnapshot s;
    s.name = layer.name();
    s.visible = layer.isVisible();
    s.opacity = layer.opacity();
    s.blendMode = layer.blendMode();
    s.offsetX = layer.offsetX();
    s.offsetY = layer.offsetY();
    s.styles = layer.styles().snapshot();
    s.filters = layer.filters().snapshot();
    if (layer.hasMask() && layer.mask()) {
        s.hasMask = true;
        s.maskEnabled = layer.mask()->isEnabled();
        s.maskLinked = layer.mask()->isLinked();
        s.maskGray = layer.mask()->image().copy();
    }
    s.linkPath = layer.linkPath();
    s.kind = layer.kind();
    return s;
}

void applyLayerProps(Layer &layer, const LayerPropSnapshot &s)
{
    layer.setName(s.name);
    layer.setVisible(s.visible);
    layer.setOpacity(s.opacity);
    layer.setBlendMode(s.blendMode);
    layer.setOffsetSilent(s.offsetX, s.offsetY);
    layer.setKindSilent(s.kind);
    layer.styles().replaceAll(s.styles);
    layer.filters().replaceAll(s.filters);
    if (s.hasMask && !s.maskGray.isNull()) {
        auto mask = std::make_unique<LayerMask>();
        mask->setFromImage(s.maskGray);
        mask->setEnabled(s.maskEnabled);
        mask->setLinked(s.maskLinked);
        layer.setMask(std::move(mask));
    } else {
        layer.setMask(nullptr);
    }
    layer.setLinkPathSilent(s.linkPath);
    layer.invalidateCompositeRaster();
}

LayerPixelsUndo::LayerPixelsUndo(int layerIndex, QImage pixels, int offsetX, int offsetY,
                                 const QString &label)
    : m_layerIndex(layerIndex)
    , m_offsetX(offsetX)
    , m_offsetY(offsetY)
    , m_pixels(std::move(pixels))
    , m_label(label)
{
}

LayerPixelsUndo::LayerPixelsUndo(int layerIndex, QImage pixels, int offsetX, int offsetY,
                                 bool hasMask, bool maskEnabled, bool maskLinked, QImage maskGray,
                                 const QString &label)
    : m_layerIndex(layerIndex)
    , m_offsetX(offsetX)
    , m_offsetY(offsetY)
    , m_pixels(std::move(pixels))
    , m_trackMask(true)
    , m_hasMask(hasMask)
    , m_maskEnabled(maskEnabled)
    , m_maskLinked(maskLinked)
    , m_maskGray(std::move(maskGray))
    , m_label(label)
{
}

quint64 LayerPixelsUndo::byteSize() const
{
    return quint64(m_pixels.sizeInBytes()) + quint64(m_maskGray.sizeInBytes()) + 64;
}

void LayerPixelsUndo::pop(ImageDocument &doc)
{
    Layer *layer = doc.m_layers.layerAt(m_layerIndex);
    if (!layer)
        return;
    QImage current = layer->materialize();
    const int curOx = layer->offsetX();
    const int curOy = layer->offsetY();

    bool curHasMask = false;
    bool curMaskEnabled = true;
    bool curMaskLinked = true;
    QImage curMaskGray;
    if (m_trackMask) {
        if (layer->hasMask() && layer->mask()) {
            curHasMask = true;
            curMaskEnabled = layer->mask()->isEnabled();
            curMaskLinked = layer->mask()->isLinked();
            curMaskGray = layer->mask()->image().copy();
        }
    }

    layer->replaceFromImage(m_pixels);
    layer->setOffsetSilent(m_offsetX, m_offsetY);

    if (m_trackMask) {
        if (m_hasMask && !m_maskGray.isNull()) {
            auto mask = std::make_unique<LayerMask>();
            mask->setFromImage(m_maskGray);
            mask->setEnabled(m_maskEnabled);
            mask->setLinked(m_maskLinked);
            layer->setMask(std::move(mask));
        } else {
            layer->setMask(nullptr);
        }
        m_hasMask = curHasMask;
        m_maskEnabled = curMaskEnabled;
        m_maskLinked = curMaskLinked;
        m_maskGray = std::move(curMaskGray);
    }

    m_pixels = std::move(current);
    m_offsetX = curOx;
    m_offsetY = curOy;
    doc.markDirty();
    emit doc.layerPropertiesChanged(m_layerIndex);
}

LayerPropUndo::LayerPropUndo(int layerIndex, LayerPropSnapshot before, const QString &label)
    : m_layerIndex(layerIndex)
    , m_snap(std::move(before))
    , m_label(label)
{
}

quint64 LayerPropUndo::byteSize() const
{
    quint64 n = sizeof(*this) + quint64(m_snap.name.size()) * 2 + 64;
    n += quint64(m_snap.styles.size()) * sizeof(LayerStyleEffect);
    n += quint64(m_snap.filters.size()) * sizeof(FilterNode);
    n += quint64(m_snap.maskGray.sizeInBytes());
    return n;
}

void LayerPropUndo::pop(ImageDocument &doc)
{
    Layer *layer = doc.m_layers.layerAt(m_layerIndex);
    if (!layer)
        return;
    const QRect docRect(0, 0, doc.m_width, doc.m_height);
    const QRect oldBounds = layer->styleBoundsInDocument().intersected(docRect);
    LayerPropSnapshot live = captureLayerProps(*layer);
    applyLayerProps(*layer, m_snap);
    m_snap = std::move(live);
    const QRect newBounds = layer->styleBoundsInDocument().intersected(docRect);
    doc.markDirty(oldBounds.united(newBounds));
    emit doc.layerPropertiesChanged(m_layerIndex);
}

std::unique_ptr<LayerStructureUndo> LayerStructureUndo::forAdded(int index, const QString &label)
{
    auto u = std::unique_ptr<LayerStructureUndo>(new LayerStructureUndo);
    u->m_kind = Kind::Added;
    u->m_index = index;
    u->m_label = label;
    return u;
}

std::unique_ptr<LayerStructureUndo> LayerStructureUndo::forRemoved(int index,
                                                                  std::unique_ptr<Layer> layer,
                                                                  int activeIndexAfter,
                                                                  const QString &label)
{
    auto u = std::unique_ptr<LayerStructureUndo>(new LayerStructureUndo);
    u->m_kind = Kind::Removed;
    u->m_index = index;
    u->m_layer = std::move(layer);
    u->m_activeIndex = activeIndexAfter;
    u->m_label = label;
    return u;
}

quint64 LayerStructureUndo::byteSize() const
{
    quint64 n = 128;
    if (m_layer && m_layer->hasPixelData())
        n += quint64(m_layer->materialize().sizeInBytes());
    return n;
}

void LayerStructureUndo::pop(ImageDocument &doc)
{
    if (m_kind == Kind::Added) {
        if (doc.layers().count() <= 1)
            return;
        auto taken = doc.takeLayerForUndo(m_index);
        if (!taken)
            return;
        if (doc.m_activeLayerIndex >= doc.layers().count())
            doc.m_activeLayerIndex = doc.layers().count() - 1;
        else if (doc.m_activeLayerIndex > m_index)
            --doc.m_activeLayerIndex;

        m_kind = Kind::Removed;
        m_layer = std::move(taken);
        m_activeIndex = doc.m_activeLayerIndex;
        doc.m_dirty = true;
        doc.m_dirtyRect = QRect(0, 0, doc.m_width, doc.m_height);
        emit doc.structureChanged();
        emit doc.activeLayerChanged(doc.m_activeLayerIndex);
        emit doc.contentChanged();
        return;
    }

    if (!m_layer)
        return;
    doc.insertLayerForUndo(m_index, std::move(m_layer));
    m_kind = Kind::Added;
    m_layer.reset();
    if (m_activeIndex >= 0 && m_activeIndex < doc.layers().count())
        doc.m_activeLayerIndex = m_activeIndex;
    doc.m_dirty = true;
    doc.m_dirtyRect = QRect(0, 0, doc.m_width, doc.m_height);
    emit doc.structureChanged();
    emit doc.activeLayerChanged(doc.m_activeLayerIndex);
    emit doc.contentChanged();
}

LayerMoveUndo::LayerMoveUndo(int from, int to, const QString &label)
    : m_from(from)
    , m_to(to)
    , m_label(label)
{
}

void LayerMoveUndo::pop(ImageDocument &doc)
{
    // 正向 from→to 后，反向 to→from；再交换记录供 redo
    doc.applyMoveLayer(m_to, m_from);
    const int tmp = m_from;
    m_from = m_to;
    m_to = tmp;
}

SelectionUndo::SelectionUndo(QImage mask, const QString &label)
    : m_mask(std::move(mask))
    , m_label(label)
{
}

quint64 SelectionUndo::byteSize() const
{
    return quint64(m_mask.sizeInBytes()) + 64;
}

void SelectionUndo::pop(ImageDocument &doc)
{
    QImage live = doc.m_selection.mask().copy();
    doc.m_selection.replaceFromImage(m_mask);
    m_mask = std::move(live);
    emit doc.selectionChanged();
}

DocumentGeomUndo::DocumentGeomUndo(int width, int height, QImage selectionMask,
                                   std::vector<LayerState> layers, int activeIndex,
                                   const QString &label)
    : m_width(width)
    , m_height(height)
    , m_activeIndex(activeIndex)
    , m_selectionMask(std::move(selectionMask))
    , m_layers(std::move(layers))
    , m_label(label)
{
}

quint64 DocumentGeomUndo::byteSize() const
{
    quint64 n = quint64(m_selectionMask.sizeInBytes()) + 64;
    for (const LayerState &ls : m_layers)
        n += quint64(ls.pixels.sizeInBytes()) + 64;
    return n;
}

void DocumentGeomUndo::pop(ImageDocument &doc)
{
    std::vector<LayerState> live;
    live.reserve(size_t(doc.layers().count()));
    for (int i = 0; i < doc.layers().count(); ++i) {
        Layer *layer = doc.layers().layerAt(i);
        if (!layer)
            continue;
        LayerState st;
        st.props = captureLayerProps(*layer);
        st.pixels = layer->materialize();
        st.width = layer->width();
        st.height = layer->height();
        live.push_back(std::move(st));
    }
    QImage liveMask = doc.m_selection.mask().copy();
    const int liveW = doc.m_width;
    const int liveH = doc.m_height;
    const int liveActive = doc.m_activeLayerIndex;

    while (doc.layers().count() > 0)
        (void)doc.takeLayerForUndo(doc.layers().count() - 1);

    doc.m_width = m_width;
    doc.m_height = m_height;
    doc.m_selection.replaceFromImage(m_selectionMask);

    for (LayerState &st : m_layers) {
        auto layer = std::make_unique<Layer>(st.props.name, st.width, st.height);
        if (!st.pixels.isNull())
            layer->replaceFromImage(st.pixels);
        applyLayerProps(*layer, st.props);
        doc.insertLayerForUndo(doc.layers().count(), std::move(layer));
    }

    doc.m_activeLayerIndex = qBound(-1, m_activeIndex, doc.layers().count() - 1);
    doc.m_dirty = true;
    doc.m_dirtyRect = QRect(0, 0, doc.m_width, doc.m_height);
    emit doc.structureChanged();
    emit doc.selectionChanged();
    emit doc.activeLayerChanged(doc.m_activeLayerIndex);
    emit doc.contentChanged();

    m_width = liveW;
    m_height = liveH;
    m_selectionMask = std::move(liveMask);
    m_layers = std::move(live);
    m_activeIndex = liveActive;
}

} // namespace Ps
