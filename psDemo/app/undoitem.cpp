/**
 * undoitem.cpp — 各 UndoItem 子类 pop() 与快照辅助函数的实现（app 层）。
 */
#include "undoitem.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layermask.h"

#include <QDebug>
#include <QPainter>
#include <utility>

namespace Ps {
namespace {

/** 把瓦片引用快照画成整幅图（安全网兜底；不消费 snap）。 */
QImage materializeTileSnap(const TileBuffer::TileRefSnapshot &snap)
{
    QImage out(snap.width, snap.height, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (snap.tilesX <= 0 || snap.images.isEmpty())
        return out;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    for (auto it = snap.images.constBegin(); it != snap.images.constEnd(); ++it) {
        const qint64 k = it.key();
        const int tx = int(k % snap.tilesX);
        const int ty = int(k / snap.tilesX);
        const int x = tx * TileBuffer::kTileSize;
        const int y = ty * TileBuffer::kTileSize;
        p.drawImage(x, y, it.value());
    }
    return out;
}

/** TileRefs：交换前比较快照与活层，只并上 cacheKey 不同 / 仅一侧有的瓦片。 */
QRect changedTilesDocRect(Layer *layer, const TileBuffer::TileRefSnapshot &snap)
{
    QRect u;
    if (!layer || snap.tilesX <= 0)
        return u;
    TileBuffer &tiles = layer->tiles();
    const int ox = layer->offsetX();
    const int oy = layer->offsetY();

    for (auto it = snap.images.constBegin(); it != snap.images.constEnd(); ++it) {
        const qint64 k = it.key();
        const int tx = int(k % snap.tilesX);
        const int ty = int(k / snap.tilesX);
        const QImage *live = tiles.tileAt(tx, ty);
        if (!live || live->cacheKey() != it.value().cacheKey())
            u = u.united(tiles.tileBounds(tx, ty).translated(ox, oy));
    }
    tiles.forEachAllocatedTile([&](int tx, int ty, const QImage &, const QRect &bounds) {
        const qint64 k = qint64(ty) * snap.tilesX + tx;
        if (!snap.images.contains(k))
            u = u.united(bounds.translated(ox, oy));
    });
    return u;
}

void swapLayerMask(Layer *layer, bool *hasMask, bool *maskEnabled, bool *maskLinked,
                   QImage *maskGray)
{
    bool curHasMask = false;
    bool curMaskEnabled = true;
    bool curMaskLinked = true;
    QImage curMaskGray;
    if (layer->hasMask() && layer->mask()) {
        curHasMask = true;
        curMaskEnabled = layer->mask()->isEnabled();
        curMaskLinked = layer->mask()->isLinked();
        curMaskGray = layer->mask()->image().copy();
    }
    if (*hasMask && !maskGray->isNull()) {
        auto mask = std::make_unique<LayerMask>();
        mask->setFromImage(*maskGray);
        mask->setEnabled(*maskEnabled);
        mask->setLinked(*maskLinked);
        layer->setMask(std::move(mask));
    } else {
        layer->setMask(nullptr);
    }
    *hasMask = curHasMask;
    *maskEnabled = curMaskEnabled;
    *maskLinked = curMaskLinked;
    *maskGray = std::move(curMaskGray);
}

} // namespace

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
    , m_extentW(m_pixels.width())
    , m_extentH(m_pixels.height())
    , m_mode(PixelUndoMode::WholeLayer)
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
    , m_extentW(m_pixels.width())
    , m_extentH(m_pixels.height())
    , m_mode(PixelUndoMode::WholeLayer)
    , m_trackMask(true)
    , m_hasMask(hasMask)
    , m_maskEnabled(maskEnabled)
    , m_maskLinked(maskLinked)
    , m_maskGray(std::move(maskGray))
    , m_label(label)
{
}

LayerPixelsUndo::LayerPixelsUndo(int layerIndex, TileBuffer::TileRefSnapshot snap, int extentW,
                                 int extentH, int offsetX, int offsetY, bool hasMask,
                                 bool maskEnabled, bool maskLinked, QImage maskGray,
                                 const QString &label, PixelUndoMode mode)
    : m_layerIndex(layerIndex)
    , m_offsetX(offsetX)
    , m_offsetY(offsetY)
    , m_tileSnap(std::move(snap))
    , m_extentW(extentW)
    , m_extentH(extentH)
    , m_mode(mode)
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
    if (m_mode == PixelUndoMode::TileRefs)
        return quint64(m_tileSnap.bytes()) + quint64(m_maskGray.sizeInBytes()) + 128;
    return quint64(m_pixels.sizeInBytes()) + quint64(m_maskGray.sizeInBytes()) + 64;
}

void LayerPixelsUndo::pop(ImageDocument &doc)
{
    Layer *layer = doc.m_layers.layerAt(m_layerIndex);
    if (!layer)
        return;

    // 交换前：活层几何 +（TileRefs）实际变化瓦片
    const QRect curBoundsDoc = layer->boundsInDocument();
    const QRect styleBefore = layer->styleBoundsInDocument();
    const int curOx = layer->offsetX();
    const int curOy = layer->offsetY();
    const int entryW = (m_mode == PixelUndoMode::WholeLayer) ? m_pixels.width() : m_extentW;
    const int entryH = (m_mode == PixelUndoMode::WholeLayer) ? m_pixels.height() : m_extentH;
    const QRect entryBoundsDoc(m_offsetX, m_offsetY, entryW, entryH);
    const QRect tileDiffDoc = (m_mode == PixelUndoMode::TileRefs)
                                  ? changedTilesDocRect(layer, m_tileSnap)
                                  : QRect();

    if (m_mode == PixelUndoMode::WholeLayer) {
        QImage current = layer->materialize();
        layer->replaceFromImage(m_pixels);
        layer->setOffsetSilent(m_offsetX, m_offsetY);
        if (m_trackMask)
            swapLayerMask(layer, &m_hasMask, &m_maskEnabled, &m_maskLinked, &m_maskGray);
        m_pixels = std::move(current);
        m_extentW = m_pixels.width();
        m_extentH = m_pixels.height();
        m_offsetX = curOx;
        m_offsetY = curOy;
    } else {
        const bool extentOk = layer->tiles().width() == m_extentW
                              && layer->tiles().height() == m_extentH;
        if (!extentOk) {
            qWarning("undo: extent 变化与 tile-ref 模式不符（调用点应用 WholeLayer）");
            Q_ASSERT(false);
            QImage current = layer->materialize();
            layer->replaceFromImage(materializeTileSnap(m_tileSnap));
            if (m_trackMask)
                swapLayerMask(layer, &m_hasMask, &m_maskEnabled, &m_maskLinked, &m_maskGray);
            m_tileSnap = TileBuffer::TileRefSnapshot{};
            m_pixels = std::move(current);
            m_mode = PixelUndoMode::WholeLayer;
            m_extentW = m_pixels.width();
            m_extentH = m_pixels.height();
            m_offsetX = curOx;
            m_offsetY = curOy;
        } else {
            Q_ASSERT(curOx == m_offsetX && curOy == m_offsetY);
            if (!layer->tiles().swapWithRefSnapshot(m_tileSnap)) {
                qWarning("undo: swapWithRefSnapshot 失败，整层兜底");
                Q_ASSERT(false);
                QImage current = layer->materialize();
                layer->replaceFromImage(materializeTileSnap(m_tileSnap));
                m_pixels = std::move(current);
                m_mode = PixelUndoMode::WholeLayer;
            }
            layer->invalidateContentBounds();
            layer->invalidateCompositeRaster();
            if (m_trackMask)
                swapLayerMask(layer, &m_hasMask, &m_maskEnabled, &m_maskLinked, &m_maskGray);
        }
    }

    // T4：WholeLayer 用两侧 extent；TileRefs 用变化瓦片并集，样式只外扩 pad（勿并整层 styleBounds）
    const QRect docRect(0, 0, doc.m_width, doc.m_height);
    QRect dirty;
    if (m_mode == PixelUndoMode::TileRefs && !tileDiffDoc.isEmpty()) {
        dirty = tileDiffDoc;
        const int pad = layer->styles().maxPadding();
        if (pad > 0)
            dirty.adjust(-pad, -pad, pad, pad);
        // 样式在交换前后 pad 相同；若曾用整层 styleBounds 会把脏区胀回整幅
        Q_UNUSED(styleBefore);
        dirty = dirty.intersected(docRect);
    } else {
        dirty = entryBoundsDoc.united(curBoundsDoc)
                    .united(styleBefore)
                    .united(layer->styleBoundsInDocument())
                    .intersected(docRect);
    }
    if (dirty.isEmpty())
        doc.markDirty();
    else
        doc.markDirty(dirty);

    // A23：像素 undo 去掉蒙版时同步清编辑标志
    if (m_trackMask && m_layerIndex == doc.m_activeLayerIndex && doc.m_editingLayerMask
        && !layer->hasMask()) {
        doc.m_editingLayerMask = false;
        emit doc.editingTargetChanged();
    }
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
    // A23：撤销去掉蒙版后须清编辑蒙版标志（否则画笔静默失效）
    if (m_layerIndex == doc.m_activeLayerIndex && doc.m_editingLayerMask
        && !layer->hasMask()) {
        doc.m_editingLayerMask = false;
        emit doc.editingTargetChanged();
    }
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
    if (!m_layer)
        return n;
    if (m_layer->hasPixelData())
        n += quint64(m_layer->materialize().sizeInBytes());
    // A25：被删层的蒙版也计入
    if (m_layer->hasMask() && m_layer->mask())
        n += quint64(m_layer->mask()->image().sizeInBytes());
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

        // A12：结构撤销删层时同步清编辑蒙版标志
        const bool wasEditingMask = doc.m_editingLayerMask;
        doc.m_editingLayerMask = false;

        m_kind = Kind::Removed;
        m_layer = std::move(taken);
        m_activeIndex = doc.m_activeLayerIndex;
        doc.m_dirty = true;
        doc.m_dirtyRect = QRect(0, 0, doc.m_width, doc.m_height);
        emit doc.structureChanged();
        emit doc.activeLayerChanged(doc.m_activeLayerIndex);
        if (wasEditingMask)
            emit doc.editingTargetChanged();
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

PathUndo::PathUndo(QVector<Path> paths, int activeIndex, const QString &label)
    : m_paths(std::move(paths))
    , m_activeIndex(activeIndex)
    , m_label(label)
{
}

quint64 PathUndo::byteSize() const
{
    quint64 n = 64;
    for (const Path &p : m_paths)
        n += quint64(p.totalAnchors()) * 64ull + 128ull;
    return n;
}

void PathUndo::pop(ImageDocument &doc)
{
    QVector<Path> live = doc.snapshotPaths();
    const int liveActive = doc.activePathIndex();
    doc.replacePathsFromUndo(m_paths, m_activeIndex);
    m_paths = std::move(live);
    m_activeIndex = liveActive;
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
    for (const LayerState &ls : m_layers) {
        n += quint64(ls.pixels.sizeInBytes()) + 64;
        // A25：与 LayerPropUndo 同口径，计入每层 maskGray
        n += quint64(ls.props.maskGray.sizeInBytes());
    }
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
    // A8：先按恢复后的文档尺寸重建 mask，再填回快照（replaceFromImage 只会缩放到「当前」mask 尺寸）
    doc.m_selection.reset(m_width, m_height);
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
