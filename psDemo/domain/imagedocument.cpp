/**
 * imagedocument.cpp — ImageDocument 图层/选区/几何/撤销与脏区广播（domain 层）。
 */
#include "imagedocument.h"

#include "app/historystack.h"
#include "app/undoitem.h"
#include "domain/filternode.h"
#include "domain/layer.h"
#include "domain/layermask.h"
#include "engine/compositor.h"
#include "engine/op/opname.h"
#include "engine/paintengine.h"
#include "io/rasterio.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QtGlobal>

#include <memory>
#include <utility>
#include <vector>

namespace Ps {

namespace {

/** 按 LayerMaskInit 生成与层同尺寸的灰度图（层局部坐标）。 */
QImage makeLayerMaskGray(const Layer &layer, const Selection &sel,
                         ImageDocument::LayerMaskInit init)
{
    const int w = layer.width();
    const int h = layer.height();
    QImage gray(w, h, QImage::Format_Grayscale8);
    if (w <= 0 || h <= 0)
        return gray;

    const bool useSelection = (init == ImageDocument::LayerMaskInit::RevealSelection
                               || init == ImageDocument::LayerMaskInit::HideSelection)
                              && !sel.isEmpty();

    if (!useSelection) {
        const bool hide = (init == ImageDocument::LayerMaskInit::HideAll
                           || init == ImageDocument::LayerMaskInit::HideSelection);
        gray.fill(hide ? 0 : 255);
        return gray;
    }

    const bool reveal = (init == ImageDocument::LayerMaskInit::RevealSelection);
    const int ox = layer.offsetX();
    const int oy = layer.offsetY();
    for (int y = 0; y < h; ++y) {
        uchar *line = gray.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const quint8 v = sel.value(ox + x, oy + y);
            line[x] = reveal ? v : quint8(255 - v);
        }
    }
    return gray;
}

} // namespace

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
        layerIndex, layer->materialize(), layer->offsetX(), layer->offsetY(), label));
}

void ImageDocument::pushLayerOffsetUndo(int layerIndex)
{
    pushLayerPropUndo(layerIndex, tr("移动图层"));
}

void ImageDocument::pushSelectionUndo(const QString &label)
{
    if (!shouldRecordHistory())
        return;
    m_history->push(std::make_unique<SelectionUndo>(m_selection.mask().copy(), label));
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
int ImageDocument::addLayer(std::unique_ptr<Layer> layer, const QString &undoLabel)
{
    if (!layer)
        return -1;

    layer->setOwner(this);
    const int index = m_layers.addLayer(std::move(layer));

    if (shouldRecordHistory()) {
        m_history->push(LayerStructureUndo::forAdded(
            index, undoLabel.isEmpty() ? tr("新建图层") : undoLabel));
    }

    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit contentChanged();
    return index;
}

/**
 * 对照 GIMP：
 * - file_open_layers：先解码成临时图，再把层 convert 进 dest_image
 * - gimp_image_add_layers(x,y,w,h)：在给定矩形内居中放置
 * 本项目瘦身：单层位图、文档矩形 = 整幅画布、直接建 Layer 入栈（无临时 GimpImage）。
 */
int ImageDocument::placeImageAsLayer(const QImage &image, const QString &name)
{
    if (image.isNull() || m_width <= 0 || m_height <= 0)
        return -1;

    const QString layerName = name.isEmpty()
                                  ? QStringLiteral("图层 %1").arg(m_layers.count() + 1)
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, image);
    // 居中：offset = (doc - layer) / 2（对照 gimp_image_add_layers 的居中公式）
    const int ox = (m_width - layer->width()) / 2;
    const int oy = (m_height - layer->height()) / 2;
    layer->setOffsetSilent(ox, oy);

    const int index = addLayer(std::move(layer), tr("置入图层"));
    if (index < 0)
        return -1;

    setActiveLayerIndex(index);
    return index;
}

/**
 * 对照 GIMP：
 * - file_open_link_image / gimp_link_layer_new：层挂 GimpLink，缓冲来自外部文件
 * - 本项目瘦身：普通 Layer + linkPath + 缓存像素；无文件监视器、无矩阵变换栈
 */
int ImageDocument::placeLinkedImageAsLayer(const QString &absolutePath, const QString &name)
{
    if (absolutePath.isEmpty() || m_width <= 0 || m_height <= 0)
        return -1;

    const QString abs = QFileInfo(absolutePath).absoluteFilePath();
    QString err;
    const QImage image = RasterIo::readFile(abs, &err);
    if (image.isNull())
        return -1;

    const QString layerName = name.isEmpty()
                                  ? QFileInfo(abs).completeBaseName()
                                  : name;
    auto layer = std::make_unique<Layer>(layerName, image);
    layer->setLinkPathSilent(abs);
    const int ox = (m_width - layer->width()) / 2;
    const int oy = (m_height - layer->height()) / 2;
    layer->setOffsetSilent(ox, oy);

    const int index = addLayer(std::move(layer), tr("置入链接图层"));
    if (index < 0)
        return -1;

    setActiveLayerIndex(index);
    return index;
}

bool ImageDocument::updateLinkedLayer(int index)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->isLinkedLayer())
        return false;

    const QString path = layer->linkPath();
    QString err;
    const QImage image = RasterIo::readFile(path, &err);
    if (image.isNull())
        return false;

    // 像素 + 属性（含 linkPath）一体可逆：尺寸可能变
    pushLayerPixelsUndo(index, tr("更新链接"));
    const int ox = layer->offsetX();
    const int oy = layer->offsetY();
    layer->replaceFromImage(image);
    layer->setOffsetSilent(ox, oy);
    layer->setLinkPathSilent(path);
    layer->invalidateContentBounds();
    layer->invalidateCompositeRaster();

    const QRect dirty = layer->styleBoundsInDocument()
                            .intersected(QRect(0, 0, m_width, m_height));
    markDirty(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

bool ImageDocument::rasterizeLinkedLayer(int index)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->isLinkedLayer())
        return false;

    // 对照 GimpRasterizable：栅格化后不再跟源文件
    pushLayerPropUndo(index, tr("栅格化链接图层"));
    layer->setLinkPathSilent(QString());
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

void ImageDocument::setActiveLayerIndex(int index)
{
    if (index < -1 || index >= m_layers.count())
        return;
    if (m_activeLayerIndex == index)
        return;
    const bool wasEditingMask = m_editingLayerMask;
    m_activeLayerIndex = index;
    m_editingLayerMask = false;
    emit activeLayerChanged(index);
    if (wasEditingMask)
        emit editingTargetChanged();
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
    if (m_selection.isEmpty())
        return;
    pushSelectionUndo(tr("取消选择"));
    m_selection.clear();
    emit selectionChanged();
}

bool ImageDocument::clearActiveLayerPixels()
{
    Layer *layer = activeLayer();
    if (!layer || !layer->isVisible())
        return false;
    if (!layer->allowsPixelEdit())
        return false; // 先栅格化再清除

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
    if (!layer->allowsPixelEdit())
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

    pushLayerPropUndo(m_activeLayerIndex, tr("亮度/对比度"));
    FilterNode node(OpName::BrightnessContrast);
    node.setBrightness(brightness);
    node.setContrast(contrast);
    const int index = layer->filters().append(node);
    layer->invalidateCompositeRaster();
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return index;
}

int ImageDocument::addAdjustmentLayer(OpName op)
{
    if (m_width <= 0 || m_height <= 0 || !isAdjustmentOp(op))
        return -1;

    // 全画布 extent：蒙版可盖住整幅；无瓦片（hasPixelData=false）
    auto layer = std::make_unique<Layer>(opNameTitle(op), m_width, m_height);
    layer->setKindSilent(LayerKind::Adjustment);
    FilterNode node(op);
    // 亮度/对比度默认略抬一点，便于立刻看见效果；其余保持中性默认
    if (op == OpName::BrightnessContrast) {
        node.setBrightness(0.12);
        node.setContrast(0.18);
    }
    layer->filters().append(node);
    // 对齐 PS：新建调整层自动挂白色蒙版（显示全部）；随层结构进同一 undo
    layer->setMask(std::make_unique<LayerMask>(m_width, m_height, 255));

    const int index = addLayer(std::move(layer), tr("新建调整图层"));
    if (index < 0)
        return -1;
    setActiveLayerIndex(index);
    return index;
}

int ImageDocument::addBrightnessContrastAdjustmentLayer(qreal brightness, qreal contrast)
{
    const int index = addAdjustmentLayer(OpName::BrightnessContrast);
    if (index < 0)
        return -1;
    Layer *layer = m_layers.layerAt(index);
    if (!layer || layer->filters().count() < 1)
        return index;
    FilterNode node = layer->filters().at(0);
    node.setBrightness(brightness);
    node.setContrast(contrast);
    layer->filters().at(0) = node;
    layer->invalidateCompositeRaster();
    markDirty();
    return index;
}

bool ImageDocument::setLayerFilterNode(int layerIndex, int filterIndex,
                                       const FilterNode &node, bool pushUndo)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || filterIndex < 0 || filterIndex >= layer->filters().count())
        return false;
    if (pushUndo)
        pushLayerPropUndo(layerIndex, tr("调整图层参数"));
    layer->filters().at(filterIndex) = node;
    layer->invalidateCompositeRaster();

    // 调整层拖参：走 live 预览（只重跑滤镜）；像素层滤镜仍整区标脏
    if (layer->isAdjustmentLayer()) {
        if (pushUndo) {
            // 脏区记下供缩略图；画布用 adopt 定稿，不必等分块 sync
            const QRect dirty =
                layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height));
            m_dirty = true;
            if (!dirty.isEmpty())
                m_dirtyRect = m_dirtyRect.isNull() ? dirty : m_dirtyRect.united(dirty);
            emit layerPropertiesChanged(layerIndex);
            emit adjustmentPreviewCommit(layerIndex);
            emit pixelsChanged(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
        } else {
            emit adjustmentPreviewChanged(layerIndex);
        }
        return true;
    }

    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    if (pushUndo)
        emit layerPropertiesChanged(layerIndex);
    return true;
}

bool ImageDocument::setLayerFilterEnabled(int layerIndex, int filterIndex, bool enabled)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || filterIndex < 0 || filterIndex >= layer->filters().count())
        return false;
    if (layer->filters().at(filterIndex).isEnabled() == enabled)
        return false;

    pushLayerPropUndo(layerIndex, tr("滤镜可见性"));
    if (!layer->filters().setEnabled(filterIndex, enabled))
        return false;
    layer->invalidateCompositeRaster();
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return true;
}

bool ImageDocument::removeLayerFilter(int layerIndex, int filterIndex)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || filterIndex < 0 || filterIndex >= layer->filters().count())
        return false;

    pushLayerPropUndo(layerIndex, tr("删除滤镜"));
    if (!layer->filters().removeAt(filterIndex))
        return false;
    layer->invalidateCompositeRaster();
    markDirty(layer->boundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    return true;
}

int ImageDocument::ensureActiveLayerStyle(LayerStyleKind kind)
{
    Layer *layer = activeLayer();
    if (!layer || !layer->isVisible())
        return -1;

    const int existing = layer->styles().indexOfKind(kind);
    if (existing >= 0 && layer->styles().at(existing).isEnabled())
        return existing;

    pushLayerPropUndo(m_activeLayerIndex, tr("图层样式"));
    const QRect before = layer->styleBoundsInDocument();
    const int index = layer->styles().ensure(kind);
    layer->invalidateCompositeRaster();
    const QRect after = layer->styleBoundsInDocument();
    markDirty(before.united(after).intersected(QRect(0, 0, m_width, m_height)));
    emit layerPropertiesChanged(activeLayerIndex());
    return index;
}

bool ImageDocument::replaceActiveLayerStyles(const QVector<LayerStyleEffect> &effects)
{
    Layer *layer = activeLayer();
    if (!layer)
        return false;

    pushLayerPropUndo(m_activeLayerIndex, tr("图层样式"));
    const QRect before = layer->styleBoundsInDocument();
    layer->styles().replaceAll(effects);
    layer->invalidateCompositeRaster();
    const QRect after = layer->styleBoundsInDocument();
    markDirty(before.united(after).intersected(QRect(0, 0, m_width, m_height)));
    emit layerPropertiesChanged(activeLayerIndex());
    return true;
}

bool ImageDocument::clearActiveLayerStyles()
{
    Layer *layer = activeLayer();
    if (!layer || layer->styles().isEmpty())
        return false;

    pushLayerPropUndo(m_activeLayerIndex, tr("清除图层样式"));
    const QRect before = layer->styleBoundsInDocument();
    layer->styles().clear();
    layer->invalidateCompositeRaster();
    markDirty(before.intersected(QRect(0, 0, m_width, m_height)));
    emit layerPropertiesChanged(activeLayerIndex());
    return true;
}

bool ImageDocument::setLayerStyleEnabled(int layerIndex, int styleIndex, bool enabled)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || styleIndex < 0 || styleIndex >= layer->styles().count())
        return false;
    if (layer->styles().at(styleIndex).isEnabled() == enabled)
        return false;

    pushLayerPropUndo(layerIndex, tr("图层样式可见性"));
    if (!layer->styles().setEnabled(styleIndex, enabled))
        return false;
    layer->invalidateCompositeRaster();
    markDirty(layer->styleBoundsInDocument().intersected(QRect(0, 0, m_width, m_height)));
    emit layerPropertiesChanged(layerIndex);
    return true;
}

void ImageDocument::selectAll()
{
    pushSelectionUndo(tr("全部选择"));
    m_selection.selectAll();
    emit selectionChanged();
}

void ImageDocument::invertSelection()
{
    pushSelectionUndo(tr("反向选择"));
    m_selection.invert();
    emit selectionChanged();
}

void ImageDocument::selectRectangle(const QRect &rect, ChannelOp op)
{
    pushSelectionUndo(tr("矩形选区"));
    m_selection.selectRectangle(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectEllipse(const QRect &rect, ChannelOp op)
{
    pushSelectionUndo(tr("椭圆选区"));
    m_selection.selectEllipse(rect, op);
    emit selectionChanged();
}

void ImageDocument::selectPolygon(const QPolygonF &points, ChannelOp op)
{
    // 经 OpRunner → SelectPolygonOp（对照 gimp_channel_select_polygon）
    pushSelectionUndo(tr("套索选区"));
    PaintEngine::selectPolygon(m_selection, points, op);
    emit selectionChanged();
}

void ImageDocument::selectFlood(const QPoint &seedDoc, int tolerance, bool contiguous,
                                bool sampleMerged, ChannelOp op)
{
    // 对照 Fuzzy Select：sample_merged → 合成 pickable；否则活动 drawable
    QImage sample;
    if (sampleMerged) {
        sample = Compositor::composite(*this);
    } else {
        Layer *layer = activeLayer();
        sample = QImage(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
        sample.fill(Qt::transparent);
        if (layer && layer->hasPixelData()) {
            QPainter p(&sample);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(layer->offsetX(), layer->offsetY(), layer->materialize());
        }
    }
    if (sample.isNull())
        return;
    if (sample.format() != QImage::Format_ARGB32_Premultiplied)
        sample = sample.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    pushSelectionUndo(tr("魔棒选区"));
    PaintEngine::selectFlood(m_selection, sample, seedDoc, tolerance, contiguous, op);
    emit selectionChanged();
}

void ImageDocument::selectLayerAlpha(int layerIndex, ChannelOp op)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer)
        return;

    pushSelectionUndo(tr("载入选区"));
    QImage pixels;
    if (layer->hasPixelData())
        pixels = layer->materialize();
    m_selection.selectFromLayerAlpha(pixels, layer->offsetX(), layer->offsetY(), op);
    emit selectionChanged();
}

void ImageDocument::selectLayerMask(int layerIndex, ChannelOp op)
{
    Layer *layer = m_layers.layerAt(layerIndex);
    if (!layer || !layer->hasMask() || !layer->mask())
        return;

    pushSelectionUndo(tr("载入蒙版为选区"));
    m_selection.selectFromLayerGray(layer->mask()->image(),
                                    layer->offsetX(), layer->offsetY(), op);
    emit selectionChanged();
}

bool ImageDocument::addLayerMask(int index, LayerMaskInit init)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || layer->width() <= 0 || layer->height() <= 0)
        return false;

    // 先按当前选区生成灰度，再取消选区：否则选区会继续裁剪蒙版画笔，
    // 「隐藏全部 / 显示选区」后的黑区永远涂不上白（看起来像蒙版没反应）。
    pushLayerPropUndo(index, tr("添加图层蒙版"));
    auto mask = std::make_unique<LayerMask>();
    mask->setFromImage(makeLayerMaskGray(*layer, m_selection, init));
    mask->setEnabled(true);
    layer->setMask(std::move(mask));

    if (!m_selection.isEmpty()) {
        m_selection.clear();
        emit selectionChanged();
    }

    // 对照 PS：添加蒙版后编辑目标切到蒙版；否则画笔仍写像素，
    // 黑蒙版下画布不变、蒙版缩略图也不变。
    const bool switchToMaskEdit = (index == m_activeLayerIndex);
    if (switchToMaskEdit)
        m_editingLayerMask = true;

    const QRect dirty = layer->styleBoundsInDocument()
                            .intersected(QRect(0, 0, m_width, m_height));
    markDirty(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
    if (switchToMaskEdit)
        emit editingTargetChanged();
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

bool ImageDocument::removeLayerMask(int index)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->hasMask())
        return false;

    pushLayerPropUndo(index, tr("删除图层蒙版"));
    const QRect dirty = layer->styleBoundsInDocument()
                            .intersected(QRect(0, 0, m_width, m_height));
    layer->setMask(nullptr);
    if (index == m_activeLayerIndex && m_editingLayerMask) {
        m_editingLayerMask = false;
        emit editingTargetChanged();
    }
    markDirty(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

bool ImageDocument::applyLayerMask(int index)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->hasMask() || !layer->mask())
        return false;

    LayerMask *mask = layer->mask();
    const bool maskEnabled = mask->isEnabled();
    const bool maskLinked = mask->isLinked();
    const QImage maskGray = mask->image().copy();

    if (shouldRecordHistory() && m_history) {
        m_history->push(std::make_unique<LayerPixelsUndo>(
            index, layer->materialize(), layer->offsetX(), layer->offsetY(),
            true, maskEnabled, maskLinked, maskGray, tr("应用图层蒙版")));
    }

    // 烘焙：alpha *= mask（停用蒙版时仍按灰度烘焙，与 PS Apply 一致）
    QImage pixels = layer->hasPixelData()
                        ? layer->materialize()
                        : QImage(layer->width(), layer->height(),
                                 QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) {
        pixels = QImage(layer->width(), layer->height(),
                        QImage::Format_ARGB32_Premultiplied);
        pixels.fill(0);
    }
    if (pixels.format() != QImage::Format_ARGB32_Premultiplied)
        pixels = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    const int w = qMin(pixels.width(), maskGray.width());
    const int h = qMin(pixels.height(), maskGray.height());
    for (int y = 0; y < h; ++y) {
        QRgb *pline = reinterpret_cast<QRgb *>(pixels.scanLine(y));
        const uchar *mline = (y < maskGray.height())
                                 ? maskGray.constScanLine(y)
                                 : nullptr;
        for (int x = 0; x < w; ++x) {
            const quint8 mv = mline ? mline[x] : quint8(0);
            const QRgb p = pline[x];
            const int a = (qAlpha(p) * int(mv) + 127) / 255;
            if (a <= 0) {
                pline[x] = 0;
                continue;
            }
            // 预乘：rgb 也按同一比例缩
            const int r = (qRed(p) * int(mv) + 127) / 255;
            const int g = (qGreen(p) * int(mv) + 127) / 255;
            const int b = (qBlue(p) * int(mv) + 127) / 255;
            pline[x] = qRgba(r, g, b, a);
        }
        // 蒙版更窄时右侧 alpha 清零
        for (int x = w; x < pixels.width(); ++x)
            pline[x] = 0;
    }
    for (int y = h; y < pixels.height(); ++y) {
        QRgb *pline = reinterpret_cast<QRgb *>(pixels.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x)
            pline[x] = 0;
    }

    layer->replaceFromImage(pixels);
    layer->setMask(nullptr);
    if (index == m_activeLayerIndex && m_editingLayerMask) {
        m_editingLayerMask = false;
        emit editingTargetChanged();
    }

    const QRect dirty = layer->styleBoundsInDocument()
                            .intersected(QRect(0, 0, m_width, m_height));
    markDirty(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

bool ImageDocument::setEditingLayerMask(bool on)
{
    if (on == m_editingLayerMask)
        return true;
    if (on) {
        Layer *layer = activeLayer();
        if (!layer || !layer->hasMask() || !layer->mask())
            return false;
    }
    m_editingLayerMask = on;
    emit editingTargetChanged();
    if (m_activeLayerIndex >= 0)
        emit layerPropertiesChanged(m_activeLayerIndex);
    return true;
}

bool ImageDocument::setLayerMaskEnabled(int index, bool enabled)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->mask() || layer->mask()->isNull())
        return false;
    if (layer->mask()->isEnabled() == enabled)
        return true;

    pushLayerPropUndo(index, enabled ? tr("启用图层蒙版") : tr("停用图层蒙版"));
    layer->mask()->setEnabled(enabled);
    const QRect dirty = layer->styleBoundsInDocument()
                            .intersected(QRect(0, 0, m_width, m_height));
    markDirty(dirty.isEmpty() ? QRect(0, 0, m_width, m_height) : dirty);
    emit layerPropertiesChanged(index);
    emit contentChanged();
    return true;
}

bool ImageDocument::setLayerMaskLinked(int index, bool linked)
{
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->mask() || layer->mask()->isNull())
        return false;
    if (layer->mask()->isLinked() == linked)
        return true;

    pushLayerPropUndo(index, linked ? tr("链接图层蒙版") : tr("取消链接图层蒙版"));
    layer->mask()->setLinked(linked);
    emit layerPropertiesChanged(index);
    return true;
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

/** 累计脏区；像素改动时使活动层内容包围盒与复合栅格缓存失效。 */
void ImageDocument::markDirty(const QRect &rect)
{
    if (rect.isEmpty())
        return;

    m_dirty = true;
    m_dirtyRect = m_dirtyRect.isNull() ? rect : m_dirtyRect.united(rect);

    if (Layer *layer = activeLayer()) {
        layer->invalidateContentBounds();
        layer->invalidateCompositeRaster();
    }

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

void ImageDocument::translateLayer(int index, int dx, int dy, bool emitContent)
{
    if (dx == 0 && dy == 0)
        return;
    Layer *layer = m_layers.layerAt(index);
    if (!layer)
        return;

    // 含样式外扩，避免投影残影；平移不改像素，勿走 markDirty（会清复合缓存）
    const QRect docRect(0, 0, m_width, m_height);
    const QRect oldBounds = layer->styleBoundsInDocument().intersected(docRect);

    // 取消链接：层动蒙版不动 → 蒙版灰度反方向平移，文档位置不变
    if (layer->mask() && !layer->mask()->isNull() && !layer->mask()->isLinked())
        layer->mask()->shift(-dx, -dy, 255);

    layer->translate(dx, dy);
    const QRect newBounds = layer->styleBoundsInDocument().intersected(docRect);

    const QRect dirty = oldBounds.united(newBounds);
    if (dirty.isEmpty())
        return;

    m_dirty = true;
    m_dirtyRect = m_dirtyRect.isNull() ? dirty : m_dirtyRect.united(dirty);

    // emitContent=false：移动工具 live 预览路径，由工具自己重画，松手再投影 sync
    if (!emitContent)
        return;

    // 对照 GIMP：拖中仍可 flush；preview_freeze 只冻缩略图
    if (!m_previewFrozen)
        emit layerPropertiesChanged(index);
    if (!m_previewFrozen)
        emit pixelsChanged(dirty);
    emit contentChanged();
}

void ImageDocument::shiftLayerMask(int index, int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return;
    Layer *layer = m_layers.layerAt(index);
    if (!layer || !layer->mask() || layer->mask()->isNull())
        return;

    const QRect docRect(0, 0, m_width, m_height);
    const QRect oldBounds = layer->styleBoundsInDocument().intersected(docRect);
    layer->mask()->shift(dx, dy, 255);
    const QRect newBounds = layer->styleBoundsInDocument().intersected(docRect);
    const QRect dirty = oldBounds.united(newBounds);
    if (dirty.isEmpty())
        return;

    m_dirty = true;
    m_dirtyRect = m_dirtyRect.isNull() ? dirty : m_dirtyRect.united(dirty);

    if (!m_previewFrozen)
        emit layerPropertiesChanged(index);
    if (!m_previewFrozen)
        emit pixelsChanged(dirty);
    emit contentChanged();
}

void ImageDocument::beginPreviewFreeze()
{
    m_previewFrozen = true;
}

void ImageDocument::endPreviewFreeze()
{
    if (!m_previewFrozen)
        return;
    m_previewFrozen = false;

    // 对照 GIMP preview_thaw：补缩略图（拖中跳过了 pixelsChanged）
    if (m_activeLayerIndex >= 0)
        emit layerPropertiesChanged(m_activeLayerIndex);
    const QRect thumbDirty = m_dirtyRect.isEmpty()
                                 ? QRect(0, 0, m_width, m_height)
                                 : m_dirtyRect;
    emit pixelsChanged(thumbDirty);
    emit contentChanged();
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
    for (int i = 0; i < src->styles().count(); ++i)
        copy->styles().append(src->styles().at(i));
    if (src->hasMask() && src->mask()) {
        auto mask = std::make_unique<LayerMask>(src->mask()->clone());
        copy->setMask(std::move(mask));
    }
    copy->setLinkPathSilent(src->linkPath());
    copy->setKindSilent(src->kind());

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

bool ImageDocument::moveLayer(int from, int to)
{
    if (from == to)
        return false;
    if (from < 0 || from >= m_layers.count() || to < 0 || to >= m_layers.count())
        return false;

    if (shouldRecordHistory())
        m_history->push(std::make_unique<LayerMoveUndo>(from, to, tr("移动图层")));

    applyMoveLayer(from, to);
    return true;
}

void ImageDocument::applyMoveLayer(int from, int to)
{
    if (from == to)
        return;
    if (from < 0 || from >= m_layers.count() || to < 0 || to >= m_layers.count())
        return;

    // 对照 GIMP gimp_drawable_stack_reorder：只脏被移层的包围盒，不整幅重合成。
    // 层文档位置不变，仅 z 序变；ROI 外其它层相对顺序不变。
    const QRect docRect(0, 0, m_width, m_height);
    QRect dirty;
    if (const Layer *moved = m_layers.layerAt(from))
        dirty = moved->styleBoundsInDocument().intersected(docRect);

    m_layers.moveLayer(from, to);

    // 活动层跟随被拖层；其余下标按区间平移（对照容器 reorder）
    if (m_activeLayerIndex == from) {
        m_activeLayerIndex = to;
    } else if (from < to) {
        if (m_activeLayerIndex > from && m_activeLayerIndex <= to)
            --m_activeLayerIndex;
    } else {
        if (m_activeLayerIndex >= to && m_activeLayerIndex < from)
            ++m_activeLayerIndex;
    }

    m_dirty = true;
    if (dirty.isEmpty()) {
        // 空层 / 越界：退回整幅（与 add/remove 一致，保证可见）
        m_dirtyRect = docRect;
        dirty = docRect;
    } else {
        m_dirtyRect = m_dirtyRect.isNull() ? dirty : m_dirtyRect.united(dirty);
    }

    emit structureChanged();
    emit activeLayerChanged(m_activeLayerIndex);
    emit pixelsChanged(dirty);
    emit contentChanged();
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

        QImage maskGray;
        bool maskEnabled = true;
        if (layer->hasMask() && layer->mask()) {
            maskGray = layer->mask()->image().scaled(
                newWidth, newHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            if (maskGray.format() != QImage::Format_Grayscale8)
                maskGray = maskGray.convertToFormat(QImage::Format_Grayscale8);
            maskEnabled = layer->mask()->isEnabled();
        }

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
        if (!maskGray.isNull()) {
            auto mask = std::make_unique<LayerMask>();
            mask->setFromImage(maskGray);
            mask->setEnabled(maskEnabled);
            layer->setMask(std::move(mask));
        }
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

        QImage maskGray;
        bool maskEnabled = true;
        if (layer->hasMask() && layer->mask() && !layer->mask()->isNull()) {
            maskGray = layer->mask()->image();
            maskEnabled = layer->mask()->isEnabled();
        }

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

        const int placeX = offsetX + layer->offsetX();
        const int placeY = offsetY + layer->offsetY();

        QPainter painter(&neu);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(placeX, placeY, src);
        painter.end();

        if (neu.format() != QImage::Format_ARGB32_Premultiplied)
            neu = neu.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        layer->replaceFromImage(neu);
        layer->setOffsetSilent(0, 0);

        if (!maskGray.isNull()) {
            QImage neuMask(newWidth, newHeight, QImage::Format_Grayscale8);
            neuMask.fill(255); // 扩展区默认显示
            for (int y = 0; y < maskGray.height(); ++y) {
                const int dy = placeY + y;
                if (dy < 0 || dy >= newHeight)
                    continue;
                const uchar *s = maskGray.constScanLine(y);
                uchar *d = neuMask.scanLine(dy);
                for (int x = 0; x < maskGray.width(); ++x) {
                    const int dx = placeX + x;
                    if (dx < 0 || dx >= newWidth)
                        continue;
                    d[dx] = s[x];
                }
            }
            auto mask = std::make_unique<LayerMask>();
            mask->setFromImage(neuMask);
            mask->setEnabled(maskEnabled);
            layer->setMask(std::move(mask));
        }
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

void ImageDocument::cropTo(const QRect &rect)
{
    // 对照 gimp_image_crop：保留 rect 内内容，文档原点移到 rect 左上
    const QRect r = rect.normalized().intersected(QRect(0, 0, m_width, m_height));
    if (r.width() < 1 || r.height() < 1)
        return;
    if (r.x() == 0 && r.y() == 0 && r.width() == m_width && r.height() == m_height)
        return;

    pushDocumentGeomUndo(tr("裁剪"));

    for (int i = 0; i < m_layers.count(); ++i) {
        Layer *layer = m_layers.layerAt(i);
        if (!layer)
            continue;

        QImage maskGray;
        bool maskEnabled = true;
        if (layer->hasMask() && layer->mask() && !layer->mask()->isNull()) {
            maskGray = layer->mask()->image();
            maskEnabled = layer->mask()->isEnabled();
        }

        const int oldOx = layer->offsetX();
        const int oldOy = layer->offsetY();

        QImage src = layer->materialize();
        if (src.isNull()) {
            src = QImage(layer->width(), layer->height(), QImage::Format_ARGB32_Premultiplied);
            src.fill(Qt::transparent);
        }

        // 先落到文档坐标，再裁切（层可能有 offset）
        QImage full(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
        full.fill(Qt::transparent);
        {
            QPainter painter(&full);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawImage(oldOx, oldOy, src);
        }

        QImage cropped = full.copy(r);
        if (cropped.format() != QImage::Format_ARGB32_Premultiplied)
            cropped = cropped.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        layer->replaceFromImage(cropped);
        layer->setOffsetSilent(0, 0);

        if (!maskGray.isNull()) {
            QImage fullMask(m_width, m_height, QImage::Format_Grayscale8);
            fullMask.fill(255);
            for (int y = 0; y < maskGray.height(); ++y) {
                const int dy = oldOy + y;
                if (dy < 0 || dy >= m_height)
                    continue;
                const uchar *s = maskGray.constScanLine(y);
                uchar *d = fullMask.scanLine(dy);
                for (int x = 0; x < maskGray.width(); ++x) {
                    const int dx = oldOx + x;
                    if (dx < 0 || dx >= m_width)
                        continue;
                    d[dx] = s[x];
                }
            }
            QImage croppedMask = fullMask.copy(r);
            if (croppedMask.format() != QImage::Format_Grayscale8)
                croppedMask = croppedMask.convertToFormat(QImage::Format_Grayscale8);
            auto mask = std::make_unique<LayerMask>();
            mask->setFromImage(croppedMask);
            mask->setEnabled(maskEnabled);
            layer->setMask(std::move(mask));
        }
    }

    // 旧 mask 平移：新原点 = 旧 (r.x, r.y)
    m_selection.resizeCanvas(r.width(), r.height(), -r.x(), -r.y());
    m_width = r.width();
    m_height = r.height();
    m_dirty = true;
    m_dirtyRect = QRect(0, 0, m_width, m_height);
    emit structureChanged();
    emit selectionChanged();
    emit contentChanged();
}

} // namespace Ps
