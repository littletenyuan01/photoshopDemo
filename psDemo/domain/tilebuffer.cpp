/**
 * tilebuffer.cpp — TileBuffer 瓦片分配、填充、整图导入与区域遍历（domain 层）。
 */
#include "tilebuffer.h"

#include <QPainter>
#include <QSet>
#include <QtMath>

#include <utility>

namespace Ps {

namespace {

bool isFullyTransparent(const QColor &color)
{
    return color.alpha() == 0;
}

qint64 imageBytes(const QImage &img)
{
    return img.isNull() ? 0 : qint64(img.sizeInBytes());
}

} // namespace

TileBuffer::TileBuffer()
{
    TileCache::instance().registerBuffer(this);
}

TileBuffer::TileBuffer(int width, int height)
{
    TileCache::instance().registerBuffer(this);
    reset(width, height);
}

TileBuffer::~TileBuffer()
{
    const qint64 bytes = residentBytes();
    m_tiles.clear();
    if (bytes > 0)
        TileCache::instance().noteBufferCleared(bytes);
    TileCache::instance().unregisterBuffer(this);
}

TileBuffer::TileBuffer(TileBuffer &&other) noexcept
    : m_width(other.m_width)
    , m_height(other.m_height)
    , m_tilesX(other.m_tilesX)
    , m_tilesY(other.m_tilesY)
    , m_tiles(std::move(other.m_tiles))
{
    TileCache::instance().unregisterBuffer(&other);
    other.m_width = other.m_height = other.m_tilesX = other.m_tilesY = 0;
    TileCache::instance().registerBuffer(this);
}

TileBuffer &TileBuffer::operator=(TileBuffer &&other) noexcept
{
    if (this == &other)
        return *this;
    const qint64 oldBytes = residentBytes();
    m_tiles.clear();
    if (oldBytes > 0)
        TileCache::instance().noteBufferCleared(oldBytes);

    m_width = other.m_width;
    m_height = other.m_height;
    m_tilesX = other.m_tilesX;
    m_tilesY = other.m_tilesY;
    m_tiles = std::move(other.m_tiles);

    TileCache::instance().unregisterBuffer(&other);
    other.m_width = other.m_height = other.m_tilesX = other.m_tilesY = 0;
    return *this;
}

qint64 TileBuffer::slotResidentBytes(const TileSlot &slot) const
{
    return imageBytes(slot.img);
}

qint64 TileBuffer::residentBytes() const
{
    qint64 sum = 0;
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it)
        sum += imageBytes(it.value().img);
    return sum;
}

void TileBuffer::reset(int width, int height)
{
    const qint64 oldBytes = residentBytes();
    m_width = qMax(0, width);
    m_height = qMax(0, height);
    m_tilesX = m_width > 0 ? (m_width + kTileSize - 1) / kTileSize : 0;
    m_tilesY = m_height > 0 ? (m_height + kTileSize - 1) / kTileSize : 0;
    m_tiles.clear();
    if (oldBytes > 0)
        TileCache::instance().noteBufferCleared(oldBytes);
}

void TileBuffer::clearTiles()
{
    const qint64 oldBytes = residentBytes();
    m_tiles.clear();
    if (oldBytes > 0)
        TileCache::instance().noteBufferCleared(oldBytes);
}

void TileBuffer::sharePixelsFrom(const TileBuffer &other)
{
    // 先把源侧全部读回内存，再 QImage 拷贝（隐式共享）
    TileBuffer &mutableOther = const_cast<TileBuffer &>(other);
    for (auto it = mutableOther.m_tiles.begin(); it != mutableOther.m_tiles.end(); ++it) {
        const qint64 k = it.key();
        const int tx = other.m_tilesX > 0 ? int(k % other.m_tilesX) : 0;
        const int ty = other.m_tilesX > 0 ? int(k / other.m_tilesX) : 0;
        mutableOther.makeResident(k, it.value(), tx, ty);
    }

    reset(other.m_width, other.m_height);
    for (auto it = other.m_tiles.constBegin(); it != other.m_tiles.constEnd(); ++it) {
        TileSlot slot;
        slot.img = it.value().img; // COW 共享像素
        slot.swapOffset = -1;      // 不共享 swap 偏移
        slot.dirty = true;         // 本缓冲视为独立脏副本（写时 detach）
        slot.lastUse = TileCache::instance().nextClock();
        m_tiles.insert(it.key(), slot);
        const qint64 bytes = imageBytes(slot.img);
        if (bytes > 0)
            TileCache::instance().noteResident(bytes);
    }
}

bool TileBuffer::validTileIndex(int tx, int ty) const
{
    return tx >= 0 && ty >= 0 && tx < m_tilesX && ty < m_tilesY;
}

QRect TileBuffer::tileBounds(int tx, int ty) const
{
    if (!validTileIndex(tx, ty))
        return {};
    const int x = tx * kTileSize;
    const int y = ty * kTileSize;
    const int w = qMin(kTileSize, m_width - x);
    const int h = qMin(kTileSize, m_height - y);
    return QRect(x, y, w, h);
}

QImage *TileBuffer::makeResident(qint64 k, TileSlot &slot, int tx, int ty)
{
    Q_UNUSED(k);
    auto &cache = TileCache::instance();
    if (!slot.img.isNull()) {
        cache.touch(slot);
        return &slot.img;
    }
    // 并行共享读应已在主线程预加载；worker 走到这里说明护栏漏了
    Q_ASSERT_X(!cache.sharedReadActive(), "TileBuffer::makeResident",
               "tile not resident during shared parallel read (A5)");
    const QRect bounds = tileBounds(tx, ty);
    if (bounds.isEmpty())
        return nullptr;

    if (slot.swapOffset >= 0) {
        QImage loaded;
        if (cache.loadTileImage(slot.swapOffset, bounds.width(), bounds.height(), &loaded)) {
            slot.img = std::move(loaded);
            slot.dirty = false;
            cache.touch(slot);
            cache.noteResident(imageBytes(slot.img));
            return &slot.img;
        }
        // 降级：读失败则新建透明块（数据可能丢，但不许崩）
    }

    slot.img = QImage(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
    slot.img.fill(Qt::transparent);
    slot.dirty = true;
    cache.touch(slot);
    cache.noteResident(imageBytes(slot.img));
    return &slot.img;
}

const QImage *TileBuffer::makeResidentConst(qint64 k, TileSlot &slot, int tx, int ty) const
{
    return const_cast<TileBuffer *>(this)->makeResident(k, slot, tx, ty);
}

QImage *TileBuffer::ensureTile(int tx, int ty)
{
    if (!validTileIndex(tx, ty))
        return nullptr;

    const qint64 k = key(tx, ty);
    auto it = m_tiles.find(k);
    if (it != m_tiles.end()) {
        QImage *img = makeResident(k, it.value(), tx, ty);
        if (img)
            TileCache::instance().markDirty(it.value());
        return img;
    }

    const QRect bounds = tileBounds(tx, ty);
    TileSlot slot;
    slot.img = QImage(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
    slot.img.fill(Qt::transparent);
    slot.dirty = true;
    slot.lastUse = TileCache::instance().nextClock();
    it = m_tiles.insert(k, std::move(slot));
    TileCache::instance().noteResident(imageBytes(it.value().img));
    return &it.value().img;
}

QImage *TileBuffer::tileAt(int tx, int ty)
{
    if (!validTileIndex(tx, ty))
        return nullptr;
    auto it = m_tiles.find(key(tx, ty));
    if (it == m_tiles.end())
        return nullptr;
    return makeResident(it.key(), it.value(), tx, ty);
}

const QImage *TileBuffer::tileAt(int tx, int ty) const
{
    if (!validTileIndex(tx, ty))
        return nullptr;
    auto it = m_tiles.find(key(tx, ty));
    if (it == m_tiles.end())
        return nullptr;
    // constFind 返回的是 const；换页读回需要改 slot —— 用非 const 查找
    auto nit = const_cast<TileBuffer *>(this)->m_tiles.find(key(tx, ty));
    return makeResidentConst(nit.key(), nit.value(), tx, ty);
}

void TileBuffer::fill(const QColor &color)
{
    if (m_width <= 0 || m_height <= 0)
        return;

    if (isFullyTransparent(color)) {
        clearTiles();
        return;
    }

    for (int ty = 0; ty < m_tilesY; ++ty) {
        for (int tx = 0; tx < m_tilesX; ++tx) {
            QImage *tile = ensureTile(tx, ty);
            if (tile)
                tile->fill(color);
        }
    }
}

void TileBuffer::setFromImage(const QImage &image)
{
    const QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    reset(src.width(), src.height());
    if (src.isNull())
        return;

    for (int ty = 0; ty < m_tilesY; ++ty) {
        for (int tx = 0; tx < m_tilesX; ++tx) {
            const QRect bounds = tileBounds(tx, ty);
            bool anyOpaque = false;
            for (int y = bounds.top(); y <= bounds.bottom() && !anyOpaque; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
                for (int x = bounds.left(); x <= bounds.right(); ++x) {
                    if (qAlpha(line[x]) != 0) {
                        anyOpaque = true;
                        break;
                    }
                }
            }
            if (!anyOpaque)
                continue;

            QImage *tile = ensureTile(tx, ty);
            if (!tile)
                continue;
            QPainter p(tile);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(0, 0, src, bounds.x(), bounds.y(), bounds.width(), bounds.height());
        }
    }
}

QImage TileBuffer::materialize() const
{
    QImage out(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (m_tiles.isEmpty() || m_width <= 0 || m_height <= 0)
        return out;

    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    forEachAllocatedTile([&](int, int, const QImage &tile, const QRect &bounds) {
        p.drawImage(bounds.topLeft(), tile);
    });
    return out;
}

void TileBuffer::forEachTileInRect(const QRect &rect, bool allocateMissing, const TileCallback &fn)
{
    if (!fn || m_tilesX <= 0 || m_tilesY <= 0)
        return;

    const QRect area = rect.intersected(QRect(0, 0, m_width, m_height));
    if (area.isEmpty())
        return;

    const int tx0 = area.left() / kTileSize;
    const int ty0 = area.top() / kTileSize;
    const int tx1 = area.right() / kTileSize;
    const int ty1 = area.bottom() / kTileSize;

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            QImage *tile = allocateMissing ? ensureTile(tx, ty) : tileAt(tx, ty);
            if (!tile)
                continue;
            fn(tx, ty, *tile, tileBounds(tx, ty));
        }
    }
}

void TileBuffer::forEachTileInRect(const QRect &rect, const ConstTileCallback &fn) const
{
    if (!fn || m_tilesX <= 0 || m_tilesY <= 0)
        return;

    const QRect area = rect.intersected(QRect(0, 0, m_width, m_height));
    if (area.isEmpty())
        return;

    const int tx0 = area.left() / kTileSize;
    const int ty0 = area.top() / kTileSize;
    const int tx1 = area.right() / kTileSize;
    const int ty1 = area.bottom() / kTileSize;

    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const QImage *tile = tileAt(tx, ty);
            if (!tile)
                continue;
            fn(tx, ty, *tile, tileBounds(tx, ty));
        }
    }
}

void TileBuffer::forEachAllocatedTile(const ConstTileCallback &fn) const
{
    if (!fn)
        return;
    TileBuffer *self = const_cast<TileBuffer *>(this);
    for (auto it = self->m_tiles.begin(); it != self->m_tiles.end(); ++it) {
        const qint64 k = it.key();
        const int tx = int(k % m_tilesX);
        const int ty = int(k / m_tilesX);
        const QImage *tile = self->makeResident(k, it.value(), tx, ty);
        if (!tile)
            continue;
        fn(tx, ty, *tile, tileBounds(tx, ty));
    }
}

void TileBuffer::visitResidentSlots(const std::function<void(qint64 key, TileSlot &slot)> &fn)
{
    if (!fn)
        return;
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (!it.value().img.isNull())
            fn(it.key(), it.value());
    }
}

qint64 TileBuffer::TileRefSnapshot::bytes() const
{
    qint64 sum = 0;
    for (auto it = images.constBegin(); it != images.constEnd(); ++it)
        sum += imageBytes(it.value());
    return sum;
}

TileBuffer::TileRefSnapshot TileBuffer::takeRefSnapshot()
{
    TileRefSnapshot snap;
    snap.width = m_width;
    snap.height = m_height;
    snap.tilesX = m_tilesX;
    snap.tilesY = m_tilesY;
    if (m_tilesX <= 0 || m_tilesY <= 0)
        return snap;

    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        const qint64 k = it.key();
        const int tx = int(k % m_tilesX);
        const int ty = int(k / m_tilesX);
        QImage *img = makeResident(k, it.value(), tx, ty);
        if (!img)
            continue;
        // QImage 拷贝 = 隐式共享引用，非像素深拷贝
        snap.images.insert(k, *img);
    }
    return snap;
}

bool TileBuffer::swapWithRefSnapshot(TileRefSnapshot &snap)
{
    if (snap.width != m_width || snap.height != m_height
        || snap.tilesX != m_tilesX || snap.tilesY != m_tilesY) {
        return false;
    }
    if (m_tilesX <= 0 || m_tilesY <= 0) {
        // 空网格：两侧都应无瓦片
        if (!m_tiles.isEmpty() || !snap.images.isEmpty())
            return false;
        return true;
    }

    TileCache &cache = TileCache::instance();
    QSet<qint64> keys;
    keys.reserve(m_tiles.size() + snap.images.size());
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = snap.images.constBegin(); it != snap.images.constEnd(); ++it)
        keys.insert(it.key());

    for (qint64 k : keys) {
        const int tx = int(k % m_tilesX);
        const int ty = int(k / m_tilesX);
        const bool inLayer = m_tiles.contains(k);
        const bool inSnap = snap.images.contains(k);

        if (inLayer && inSnap) {
            TileSlot &slot = m_tiles[k];
            QImage *live = makeResident(k, slot, tx, ty);
            if (!live)
                continue;
            const qint64 oldBytes = imageBytes(slot.img);
            QImage &held = snap.images[k];
            const qint64 newBytes = imageBytes(held);
            using std::swap;
            swap(slot.img, held);
            if (oldBytes != newBytes) {
                if (oldBytes > 0)
                    cache.noteEvicted(oldBytes);
                if (newBytes > 0)
                    cache.noteResident(newBytes);
            }
            cache.markDirty(slot);
            cache.touch(slot);
        } else if (inSnap) {
            // 层里没有：新建透明格再交换（恢复快照中的旧格）
            QImage *created = ensureTile(tx, ty);
            if (!created)
                continue;
            TileSlot &slot = m_tiles[k];
            const qint64 oldBytes = imageBytes(slot.img);
            QImage &held = snap.images[k];
            const qint64 newBytes = imageBytes(held);
            using std::swap;
            swap(slot.img, held);
            if (oldBytes != newBytes) {
                if (oldBytes > 0)
                    cache.noteEvicted(oldBytes);
                if (newBytes > 0)
                    cache.noteResident(newBytes);
            }
            cache.markDirty(slot);
            cache.touch(slot);
        } else {
            // 只有层有：交给快照并删除（= 回到未分配/透明）
            TileSlot &slot = m_tiles[k];
            QImage *live = makeResident(k, slot, tx, ty);
            if (!live) {
                m_tiles.remove(k);
                continue;
            }
            const qint64 bytes = imageBytes(slot.img);
            snap.images.insert(k, std::move(slot.img));
            m_tiles.remove(k);
            if (bytes > 0)
                cache.noteEvicted(bytes);
        }
    }
    return true;
}

void TileBuffer::eraseFullyTransparentTiles()
{
    // 预留：本计划不调用。全透明回收属 out of scope。
}

} // namespace Ps
