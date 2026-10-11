/**
 * layer.cpp — Layer 属性 setter、坐标换算与内容包围盒扫描（domain 层）。
 */
#include "layer.h"

#include "imagedocument.h"
#include "engine/layerstyleeval.h"
#include "engine/op/tilepatch.h"

#include <QFileInfo>
#include <QPointF>
#include <QRect>
#include <QtGlobal>

#include <cstring>

namespace Ps {

/** 透明新建：只预定 extent，不分配瓦片。 */
Layer::Layer(const QString &name, int width, int height)
    : m_name(name)
    , m_tiles(width, height)
{
    // 故意不 ensureTile / fill：透明层 0 块瓦片，对齐 GIMP 懒分配
}

/** 打开图片：整图拆入 TileBuffer。 */
Layer::Layer(const QString &name, const QImage &pixels)
    : m_name(name)
{
    // 打开图片：整图拆成 64×64 瓦片写入
    m_tiles.setFromImage(pixels);
}

void Layer::notifyPropertiesChanged()
{
    // 未入栈（无 owner）时静默：避免临时 Layer 误广播
    if (m_owner)
        m_owner->notifyLayerPropertiesChanged(*this);
}

void Layer::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    // 改名不影响投影像素；只刷新面板标签
    if (m_owner)
        m_owner->notifyLayerLabelChanged(*this);
}

void Layer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    notifyPropertiesChanged();
}

void Layer::setOpacity(qreal opacity)
{
    const qreal clamped = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_opacity, clamped))
        return;
    m_opacity = clamped;
    notifyPropertiesChanged();
}

void Layer::setBlendMode(BlendMode mode)
{
    if (m_blendMode == mode)
        return;
    m_blendMode = mode;
    notifyPropertiesChanged();
}

void Layer::setOffset(int x, int y)
{
    if (m_offsetX == x && m_offsetY == y)
        return;
    m_offsetX = x;
    m_offsetY = y;
    notifyPropertiesChanged();
}

void Layer::setOffsetSilent(int x, int y)
{
    m_offsetX = x;
    m_offsetY = y;
}

void Layer::translate(int dx, int dy)
{
    // 只改数值；广播由 ImageDocument::translateLayer 按脏区统一发
    if (dx == 0 && dy == 0)
        return;
    m_offsetX += dx;
    m_offsetY += dy;
}

QPointF Layer::toLayerLocal(const QPointF &imagePos) const
{
    return QPointF(imagePos.x() - m_offsetX, imagePos.y() - m_offsetY);
}

QRect Layer::boundsInDocument() const
{
    return QRect(m_offsetX, m_offsetY, width(), height());
}

QRect Layer::styleBoundsInDocument() const
{
    QRect b = boundsInDocument();
    const int pad = m_styles.maxPadding();
    if (pad > 0)
        b.adjust(-pad, -pad, pad, pad);
    return b;
}

void Layer::invalidateCompositeRaster() const
{
    ++m_contentGeneration; // 见 contentGeneration()：异步定稿结果的**确定性**有效性判据
    m_compositeRasterValid = false;
    m_compositeRaster = QImage();
    m_compositeOriginDx = 0;
    m_compositeOriginDy = 0;
    // 区域滤镜缓存与整层缓存**同源失效**：像素写入 / 滤镜参数 / 样式 / 尺寸变更都汇聚到
    // 这个方法，所以它一并清掉即可，无需在别处再挂失效点（少一处漏挂的机会）。
    m_regionFilterRect = QRect();
    m_regionFilterImage = QImage();
}

void Layer::setRegionFilterResult(const QRect &regionDoc, const QImage &image)
{
    if (regionDoc.isEmpty() || image.isNull() || image.size() != regionDoc.size())
        return; // 契约不符就不写：宁可没有缓存（退回同步求值），也不要贴错位置
    m_regionFilterRect = regionDoc;
    m_regionFilterImage = image;
}

void Layer::clearRegionFilterResult()
{
    if (m_regionFilterRect.isNull() && m_regionFilterImage.isNull())
        return;
    m_regionFilterRect = QRect();
    m_regionFilterImage = QImage();
}

bool Layer::regionFilterCovers(const QRect &regionDoc) const
{
    return !m_regionFilterImage.isNull() && !regionDoc.isEmpty()
           && m_regionFilterRect.contains(regionDoc);
}

void Layer::setCompositePreview(const QImage &image, int docX, int docY)
{
    m_compositePreview = image;
    m_compositePreviewDocX = docX;
    m_compositePreviewDocY = docY;
    invalidateCompositeRaster();
}

void Layer::clearCompositePreview()
{
    if (m_compositePreview.isNull())
        return;
    m_compositePreview = QImage();
    m_compositePreviewDocX = 0;
    m_compositePreviewDocY = 0;
    invalidateCompositeRaster();
}

Layer::CompositeRaster Layer::ensureCompositeRaster() const
{
    if (m_compositeRasterValid) {
        CompositeRaster hit;
        hit.image = m_compositeRaster;
        hit.originDx = m_compositeOriginDx;
        hit.originDy = m_compositeOriginDy;
        return hit;
    }

    QImage base = materialize();
    if (m_filters.hasEnabled())
        base = m_filters.apply(base);

    CompositeRaster out;
    if (m_styles.hasEnabled()) {
        const StyledLayerResult styled = LayerStyleEval::apply(base, m_styles);
        out.image = styled.image;
        out.originDx = styled.originDx;
        out.originDy = styled.originDy;
    } else {
        out.image = base;
        out.originDx = 0;
        out.originDy = 0;
    }

    m_compositeRaster = out.image;
    m_compositeOriginDx = out.originDx;
    m_compositeOriginDy = out.originDy;
    m_compositeRasterValid = true;
    return out;
}

Layer::CompositeRegion Layer::compositeRasterRegion(const QRect &regionDoc) const
{
    CompositeRegion out;

    // 有启用样式：样式要外扩 halo（投影/描边…），区域化必须带 pad 再裁回。
    // 有**几何算子**（位移）：它读「别处」的输入，逐块求值会在块界错位。
    // 有**位置相关算子**（添加杂色）：输出依赖像素绝对位置，逐块求值会让噪声在块界错位/重复。
    // 三者都退回「整层求值一次，再按请求区裁」，见 docs/engine/roi.md §5。
    // 也因此**不**吃下面的区域滤镜缓存（那个是逐块几何）。
    if (m_styles.hasEnabled() || m_filters.hasGeometryOp() || m_filters.hasPositionalOp()) {
        const CompositeRaster whole = ensureCompositeRaster();
        out.image = whole.image;
        out.docX = offsetX() + whole.originDx;
        out.docY = offsetY() + whole.originDy;
        return out;
    }

    // 层内坐标的需求区，且必须夹在层范围内（TilePatch::extract 要求 rect 非负）
    const QRect local = regionDoc.translated(-offsetX(), -offsetY());
    const QRect layerRect(0, 0, m_tiles.width(), m_tiles.height());
    const QRect want = local.intersected(layerRect);
    if (want.isEmpty())
        return out; // 空图 → 调用方跳过该层

    // 异步「定稿」结果命中：后台已按 (want + pad) 求值过，这里只裁出 want ——
    // 于是重滤镜的整层重算不再发生在合成路径上（这是「异步/可取消定稿」的落点）。
    if (regionFilterCovers(regionDoc)) {
        out.image = m_regionFilterImage.copy(regionDoc.translated(-m_regionFilterRect.topLeft()));
        out.docX = regionDoc.left();
        out.docY = regionDoc.top();
        return out;
    }

    // 面积算子要读「请求区外扩 totalPad」的真实像素，再裁回 want：
    // 只取 want+pad（越出层范围的由 extract 填透明 = 层外即透明的 abyss），
    // 求值后裁回 want。点滤镜 totalPad()==0 时退化为原路径。
    const int pad = m_filters.totalPad();
    const QRect srcWant = want.adjusted(-pad, -pad, pad, pad).intersected(layerRect);
    QImage base = TilePatch::extract(m_tiles, srcWant);
    if (m_filters.hasEnabled()) {
        // ROI 通用化第 ① 步：把「请求区 / 所需输入区」按**本文档坐标**告诉滤镜链。
        // 本例里 srcWant 比 want 大一圈 pad（面积算子外扩），算子据此知道多出的边在哪。
        const QRect srcDoc = srcWant.translated(offsetX(), offsetY());
        const QRect outDoc = want.translated(offsetX(), offsetY());
        base = m_filters.apply(base, srcDoc, outDoc, nullptr);
    }
    if (srcWant != want) {
        const int dx = want.left() - srcWant.left();
        const int dy = want.top() - srcWant.top();
        base = base.copy(dx, dy, want.width(), want.height());
    }

    out.image = base;
    out.docX = want.left() + offsetX();
    out.docY = want.top() + offsetY();
    return out;
}

void Layer::invalidateContentBounds() const
{
    m_contentBoundsValid = false;
}

QRect Layer::computeContentBoundsLocal() const
{
    if (!hasPixelData())
        return QRect();

    int minX = width();
    int minY = height();
    int maxX = -1;
    int maxY = -1;

    m_tiles.forEachAllocatedTile([&](int /*tx*/, int /*ty*/, const QImage &tile, const QRect &bounds) {
        for (int y = 0; y < tile.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(tile.constScanLine(y));
            for (int x = 0; x < tile.width(); ++x) {
                if (qAlpha(line[x]) == 0)
                    continue;
                const int lx = bounds.x() + x;
                const int ly = bounds.y() + y;
                if (lx < minX) minX = lx;
                if (ly < minY) minY = ly;
                if (lx > maxX) maxX = lx;
                if (ly > maxY) maxY = ly;
            }
        }
    });

    if (maxX < minX || maxY < minY)
        return QRect();
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QRect Layer::contentBoundsInDocument() const
{
    // 【功能】PS 变换控件框：非透明像素最小外接矩形（文档坐标）
    if (!m_contentBoundsValid) {
        m_contentBoundsLocal = computeContentBoundsLocal();
        m_contentBoundsValid = true;
    }
    if (m_contentBoundsLocal.isEmpty())
        return QRect();
    return m_contentBoundsLocal.translated(m_offsetX, m_offsetY);
}

qreal Layer::opacityAtDocumentPos(int docX, int docY) const
{
    // 【功能】点选命中测试：读层内预乘 alpha（对照 gimp_pickable_get_opacity_at）
    if (!m_visible || m_opacity <= 0.0 || !hasPixelData())
        return 0.0;

    const int lx = docX - m_offsetX;
    const int ly = docY - m_offsetY;
    if (lx < 0 || ly < 0 || lx >= width() || ly >= height())
        return 0.0;

    const int tx = lx / TileBuffer::kTileSize;
    const int ty = ly / TileBuffer::kTileSize;
    const QImage *tile = m_tiles.tileAt(tx, ty);
    if (!tile)
        return 0.0;

    const QRect bounds = m_tiles.tileBounds(tx, ty);
    const int px = lx - bounds.x();
    const int py = ly - bounds.y();
    if (px < 0 || py < 0 || px >= tile->width() || py >= tile->height())
        return 0.0;

    int a = qAlpha(tile->pixel(px, py));
    if (m_mask && m_mask->isEnabled() && !m_mask->isNull())
        a = (a * int(m_mask->valueAt(lx, ly)) + 127) / 255;
    return (a / 255.0) * m_opacity;
}

void Layer::setMask(std::unique_ptr<LayerMask> mask)
{
    m_mask = std::move(mask);
}

bool Layer::isLinkBroken() const
{
    // 对照 gimp_link_is_broken：源文件不可读
    if (m_linkPath.isEmpty())
        return false;
    return !QFileInfo::exists(m_linkPath);
}

void Layer::setLinkPathSilent(const QString &path)
{
    m_linkPath = path;
}

void Layer::fill(const QColor &color)
{
    // 委托瓦片缓冲：透明 → clearTiles；实色 → 全格分配
    m_tiles.fill(color);
    invalidateContentBounds();
    invalidateCompositeRaster();
}

void Layer::replaceFromImage(const QImage &pixels)
{
    // 尺寸可能变化（图像大小 / 画布大小）；不在此发属性信号
    const int oldW = width();
    const int oldH = height();
    m_tiles.setFromImage(pixels);
    if (m_mask && (m_mask->width() != width() || m_mask->height() != height())) {
        // 尺寸变了：蒙版跟到新尺寸，缺省全白（避免错位）；undo 会整栈恢复
        m_mask = std::make_unique<LayerMask>(width(), height(), 255);
    }
    Q_UNUSED(oldW);
    Q_UNUSED(oldH);
    invalidateContentBounds();
    invalidateCompositeRaster();
}

QPoint Layer::expandToIncludeLocal(const QRect &localNeeded)
{
    if (localNeeded.isEmpty())
        return {};

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0)
        return {};

    const int padL = qMax(0, -localNeeded.left());
    const int padT = qMax(0, -localNeeded.top());
    const int padR = qMax(0, localNeeded.right() - (w - 1));
    const int padB = qMax(0, localNeeded.bottom() - (h - 1));
    if (padL == 0 && padT == 0 && padR == 0 && padB == 0)
        return {};

    const int newW = w + padL + padR;
    const int newH = h + padT + padB;

    QImage neu(newW, newH, QImage::Format_ARGB32_Premultiplied);
    neu.fill(0);
    if (hasPixelData()) {
        const QImage old = materialize();
        // 旧像素贴到新缓冲的 (padL,padT)；offset 同步左上移，文档位置不变
        for (int y = 0; y < old.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(old.constScanLine(y));
            QRgb *dst = reinterpret_cast<QRgb *>(neu.scanLine(y + padT));
            memcpy(dst + padL, src, size_t(old.width()) * sizeof(QRgb));
        }
    }
    m_tiles.setFromImage(neu);
    if (m_mask)
        m_mask->expand(padL, padT, padR, padB, 255);
    m_offsetX -= padL;
    m_offsetY -= padT;
    invalidateContentBounds();
    invalidateCompositeRaster();
    notifyPropertiesChanged();
    return QPoint(padL, padT);
}

} // namespace Ps
