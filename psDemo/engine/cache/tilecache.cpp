/**
 * tilecache.cpp — 预算记账、LRU 换页、粗粒度钉住、失败降级。
 */
#include "tilecache.h"

#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/tilebuffer.h"
#include "engine/projection.h"

#include <QByteArray>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace Ps {

namespace {

qint64 envBudgetBytes()
{
    const QByteArray v = qgetenv("PSLITE_TILE_CACHE_MB");
    if (v.isEmpty())
        return 0;
    bool ok = false;
    const qint64 mb = v.toLongLong(&ok);
    if (!ok || mb < 0)
        return 0;
    return mb * 1024 * 1024;
}

bool cacheLogEnabled()
{
    return qgetenv("PSLITE_TILE_CACHE_LOG") == "1";
}

constexpr int kSwapPayloadBytes = 64 * 64 * 4;

QByteArray packTile(const QImage &img)
{
    QByteArray out(kSwapPayloadBytes, char(0));
    if (img.isNull())
        return out;
    const QImage src = (img.format() == QImage::Format_ARGB32_Premultiplied)
                           ? img
                           : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = qMin(src.width(), 64);
    const int h = qMin(src.height(), 64);
    for (int y = 0; y < h; ++y) {
        const uchar *line = src.constScanLine(y);
        memcpy(out.data() + y * 64 * 4, line, size_t(w * 4));
    }
    return out;
}

bool unpackTile(const QByteArray &bytes, int w, int h, QImage *out)
{
    if (!out || bytes.size() < kSwapPayloadBytes || w <= 0 || h <= 0)
        return false;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        memcpy(img.scanLine(y), bytes.constData() + y * 64 * 4, size_t(w * 4));
    *out = std::move(img);
    return true;
}

} // namespace

TileCache &TileCache::instance()
{
    static TileCache g;
    return g;
}

TileCache::TileCache()
{
    m_budgetBytes = envBudgetBytes();
    m_log = cacheLogEnabled();
    m_swapEnabled = m_budgetBytes > 0;
}

void TileCache::setBudgetBytes(qint64 bytes)
{
    m_budgetBytes = qMax(qint64(0), bytes);
    m_swapEnabled = m_budgetBytes > 0;
    m_overBudgetLogged = false;
    if (m_budgetBytes == 0)
        m_swap.reset();
}

TileCache::Stats TileCache::stats() const
{
    Stats s;
    s.memBytes = m_usedBytes;
    s.budgetBytes = m_budgetBytes;
    s.evicted = m_evicted;
    s.loaded = m_loaded;
    s.swapFailures = m_swapFailures;
    s.swapEnabled = swapEnabled();
    if (m_swap)
        s.swapBytes = m_swap->writtenBytes();
    return s;
}

void TileCache::resetForTest()
{
    m_budgetBytes = envBudgetBytes();
    m_log = cacheLogEnabled();
    m_usedBytes = 0;
    m_clock.store(1, std::memory_order_relaxed);
    m_evicted = 0;
    m_loaded = 0;
    m_swapFailures = 0;
    m_swapEnabled = m_budgetBytes > 0;
    m_overBudgetLogged = false;
    m_trimming = false;
    m_sharedReadDepth.store(0, std::memory_order_relaxed);
    m_sharedReadPinned.clear();
    m_swap.reset();
    m_pinned.clear();
}

void TileCache::setSwapStoreForTest(std::unique_ptr<SwapStore> store)
{
    m_swap = std::move(store);
    m_swapEnabled = m_budgetBytes > 0 && m_swap && m_swap->isOpen();
}

void TileCache::registerBuffer(TileBuffer *b)
{
    if (b)
        m_buffers.insert(b);
}

void TileCache::unregisterBuffer(TileBuffer *b)
{
    if (!b)
        return;
    m_buffers.remove(b);
    m_pinned.remove(b);
}

void TileCache::pin(const TileBuffer *b)
{
    if (b)
        m_pinned.insert(b);
}

void TileCache::unpin(const TileBuffer *b)
{
    if (b)
        m_pinned.remove(b);
}

bool TileCache::isPinned(const TileBuffer *b) const
{
    return b && m_pinned.contains(b);
}

void TileCache::beginSharedRead(const ImageDocument *doc)
{
    if (!doc)
        return;

    // 最外层：主线程钉住并预加载；嵌套调用只加深 depth
    if (m_sharedReadDepth.load(std::memory_order_relaxed) == 0) {
        const LayerStack &stack = doc->layers();
        for (int i = 0; i < stack.count(); ++i) {
            Layer *layer = const_cast<Layer *>(stack.layerAt(i));
            if (!layer)
                continue;
            TileBuffer *buf = &layer->tiles();
            pin(buf);
            m_sharedReadPinned.append(buf);
            // forEachAllocatedTile → makeResident：把 swap 中的瓦片读回主线程
            buf->forEachAllocatedTile(
                [](int, int, const QImage &, const QRect &) {});
        }
    }
    m_sharedReadDepth.fetch_add(1, std::memory_order_release);
}

void TileCache::endSharedRead(const ImageDocument *doc)
{
    Q_UNUSED(doc);
    const int prev = m_sharedReadDepth.fetch_sub(1, std::memory_order_acq_rel);
    Q_ASSERT(prev > 0);
    if (prev != 1)
        return;
    for (const TileBuffer *b : m_sharedReadPinned)
        unpin(b);
    m_sharedReadPinned.clear();
}

qint64 TileCache::nextClock()
{
    return m_clock.fetch_add(1, std::memory_order_relaxed) + 1;
}

void TileCache::touch(TileSlot &slot)
{
    // 并行共享读期间禁止写 LRU / 时钟（worker 只读已驻留瓦片）
    if (sharedReadActive())
        return;
    slot.lastUse = nextClock();
}

void TileCache::markDirty(TileSlot &slot)
{
    slot.dirty = true;
    touch(slot);
}

void TileCache::noteResident(qint64 bytes)
{
    m_usedBytes += bytes;
    checkBudgetAndLog();
}

void TileCache::noteEvicted(qint64 bytes)
{
    if (bytes > m_usedBytes)
        m_usedBytes = 0;
    else
        m_usedBytes -= bytes;
}

void TileCache::noteBufferCleared(qint64 releasedBytes)
{
    noteEvicted(releasedBytes);
}

void TileCache::checkBudgetAndLog()
{
    if (m_budgetBytes <= 0 || m_usedBytes <= m_budgetBytes) {
        m_overBudgetLogged = false;
        return;
    }
    if (m_overBudgetLogged)
        return;
    m_overBudgetLogged = true;
    std::printf("[tilecache] over budget: used=%.2fMB budget=%.2fMB (evict at silent trim)\n",
                m_usedBytes / (1024.0 * 1024.0), m_budgetBytes / (1024.0 * 1024.0));
    std::fflush(stdout);
}

void TileCache::ensureSwapStore()
{
    if (m_swap || !m_swapEnabled || m_budgetBytes <= 0)
        return;
    m_swap = std::make_unique<FileSwapStore>();
    if (!m_swap->isOpen())
        disableSwap("cannot open swap file");
}

void TileCache::disableSwap(const char *why)
{
    ++m_swapFailures;
    if (m_swapEnabled) {
        std::printf("[tilecache] swap disabled: %s\n", why ? why : "unknown");
        std::fflush(stdout);
    }
    m_swapEnabled = false;
    m_swap.reset();
}

qint64 TileCache::storeTileImage(const QImage &img)
{
    ensureSwapStore();
    if (!swapEnabled() || !m_swap)
        return -1;
    const qint64 off = m_swap->put(packTile(img));
    if (off < 0) {
        disableSwap("swap put failed");
        return -1;
    }
    return off;
}

bool TileCache::loadTileImage(qint64 swapOffset, int w, int h, QImage *out)
{
    if (swapOffset < 0 || !out)
        return false;
    ensureSwapStore();
    if (!m_swap || !m_swap->isOpen()) {
        disableSwap("swap get: store closed");
        return false;
    }
    QByteArray bytes;
    if (!m_swap->get(swapOffset, kSwapPayloadBytes, &bytes)) {
        disableSwap("swap get failed");
        return false;
    }
    if (!unpackTile(bytes, w, h, out))
        return false;
    ++m_loaded;
    return true;
}

bool TileCache::writeSlotToSwap(TileSlot &slot)
{
    if (slot.img.isNull())
        return false;
    const qint64 off = storeTileImage(slot.img);
    if (off < 0)
        return false;
    slot.swapOffset = off;
    slot.dirty = false;
    return true;
}

void TileCache::evictDerivedCaches(ImageDocument *doc, Projection *projection)
{
    if (doc) {
        for (int i = 0; i < doc->layers().count(); ++i) {
            if (Layer *layer = doc->layers().layerAt(i))
                layer->clearRegionFilterResult();
        }
    }
    if (projection)
        projection->invalidateChunkValidity();
}

void TileCache::evictOneTile()
{
    TileBuffer *bestOwner = nullptr;
    qint64 bestKey = -1;
    qint64 bestUse = std::numeric_limits<qint64>::max();
    TileSlot *bestSlot = nullptr;

    for (TileBuffer *buf : m_buffers) {
        if (!buf || isPinned(buf))
            continue;
        buf->visitResidentSlots([&](qint64 key, TileSlot &slot) {
            if (slot.img.isNull())
                return;
            if (slot.lastUse < bestUse) {
                bestUse = slot.lastUse;
                bestOwner = buf;
                bestKey = key;
                bestSlot = &slot;
            }
        });
    }

    if (!bestSlot || !bestOwner)
        return;

    const qint64 bytes = bestOwner->slotResidentBytes(*bestSlot);
    if (bestSlot->dirty || bestSlot->swapOffset < 0) {
        if (!writeSlotToSwap(*bestSlot))
            return;
    }
    bestSlot->img = QImage();
    noteEvicted(bytes);
    ++m_evicted;
    if (m_log) {
        std::printf("[tilecache] evict key=%lld bytes=%lld used=%lld\n",
                    static_cast<long long>(bestKey), static_cast<long long>(bytes),
                    static_cast<long long>(m_usedBytes));
        std::fflush(stdout);
    }
}

void TileCache::trim(ImageDocument *doc, Projection *projection)
{
    if (m_trimming || m_budgetBytes <= 0 || sharedReadActive())
        return;
    if (m_usedBytes <= m_budgetBytes)
        return;

    m_trimming = true;
    evictDerivedCaches(doc, projection);

    int guard = 0;
    constexpr int kMaxEvict = 100000;
    while (m_usedBytes > m_budgetBytes && swapEnabled() && guard < kMaxEvict) {
        const qint64 before = m_usedBytes;
        evictOneTile();
        if (m_usedBytes >= before)
            break;
        ++guard;
    }

    m_trimming = false;
    checkBudgetAndLog();
}

} // namespace Ps
