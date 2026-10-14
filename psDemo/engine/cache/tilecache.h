/**
 * tilecache.h — 全局瓦片页缓存（预算 / LRU / 粗粒度钉住 / 换页）。
 *
 * 对照 GIMP gimptile-cache，但**不做** per-tile ref_count（见 page-cache-plan §3）。
 * 默认预算 0 = 关闭换页（与 GPU opt-in 同一哲学）。
 */
#ifndef ENGINE_CACHE_TILECACHE_H
#define ENGINE_CACHE_TILECACHE_H

#include "swapstore.h"

#include <QImage>
#include <QSet>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace Ps {

class TileBuffer;
class ImageDocument;
class Projection;

/** 单瓦片在 TileBuffer 内的槽（内存 / swap；未分配 = hash 无此键）。 */
struct TileSlot {
    QImage img;             ///< null = 当前不在内存（可能在 swap）
    qint64 swapOffset = -1; ///< ≥0 = 已有一份 swap 副本
    bool dirty = false;     ///< 内存新于 swap（或尚未写过 swap）
    qint64 lastUse = 0;     ///< LRU 时钟
};

/**
 * 全局瓦片缓存单例。
 *
 * 【淘汰纪律】绝不在 ensureTile 里 trim；只在静默点调用 trim()。
 * 【钉住】操作期间 pin(buffer)，淘汰跳过被钉住的 buffer。
 * 【失败降级】swap 失败 ⇒ 瓦片留内存、记一次日志、关闭换页。
 */
class TileCache
{
public:
    static TileCache &instance();

    struct Stats {
        qint64 memBytes = 0;
        qint64 swapBytes = 0;
        qint64 budgetBytes = 0;
        int evicted = 0;
        int loaded = 0;
        int swapFailures = 0;
        bool swapEnabled = false;
    };

    /** 0 = 不淘汰 / 不换页。也可由 PSLITE_TILE_CACHE_MB 覆盖。 */
    void setBudgetBytes(qint64 bytes);
    qint64 budgetBytes() const { return m_budgetBytes; }
    qint64 usedBytes() const { return m_usedBytes; }

    Stats stats() const;

    void noteResident(qint64 bytes);
    void noteEvicted(qint64 bytes);
    void noteBufferCleared(qint64 releasedBytes);

    /** 从 swap 读回为 w×h 预乘图；失败返回 false（已记降级）。 */
    bool loadTileImage(qint64 swapOffset, int w, int h, QImage *out);
    /** 把瓦片写入 swap；成功返回偏移，失败返回 -1。 */
    qint64 storeTileImage(const QImage &img);

    void touch(TileSlot &slot);
    void markDirty(TileSlot &slot);

    void pin(const TileBuffer *b);
    void unpin(const TileBuffer *b);
    bool isPinned(const TileBuffer *b) const;

    /**
     * 并行合成前（主线程）：钉住文档全部层瓦片并确保驻留内存。
     * 期间 touch/换页记账对 worker 只读路径关闭（A5：跨线程 makeResident/touch 数据竞争）。
     * 必须与 endSharedRead 成对。
     */
    void beginSharedRead(const ImageDocument *doc);
    void endSharedRead(const ImageDocument *doc);
    bool sharedReadActive() const { return m_sharedReadDepth.load(std::memory_order_acquire) > 0; }

    /**
     * 静默点淘汰：先丢可重算派生缓存，再按 LRU 换出未钉住瓦片。
     * @p doc / @p projection 可空。
     */
    void trim(ImageDocument *doc = nullptr, Projection *projection = nullptr);

    void checkBudgetAndLog();

    void registerBuffer(TileBuffer *b);
    void unregisterBuffer(TileBuffer *b);

    void setSwapStoreForTest(std::unique_ptr<SwapStore> store);
    void resetForTest();

    bool swapEnabled() const { return m_swapEnabled && m_budgetBytes > 0; }

    qint64 nextClock();

private:
    TileCache();
    void ensureSwapStore();
    void disableSwap(const char *why);
    bool writeSlotToSwap(TileSlot &slot);
    void evictDerivedCaches(ImageDocument *doc, Projection *projection);
    void evictOneTile();

    qint64 m_budgetBytes = 0;
    qint64 m_usedBytes = 0;
    std::atomic<qint64> m_clock{1};
    int m_evicted = 0;
    int m_loaded = 0;
    int m_swapFailures = 0;
    bool m_swapEnabled = true;
    bool m_overBudgetLogged = false;
    bool m_log = false;
    bool m_trimming = false;
    std::atomic<int> m_sharedReadDepth{0};

    std::unique_ptr<SwapStore> m_swap;
    QSet<const TileBuffer *> m_pinned;
    QSet<TileBuffer *> m_buffers;
    QVector<const TileBuffer *> m_sharedReadPinned;
};

/** RAII：并行读文档瓦片（主线程构造/析构）。 */
class TileSharedReadGuard
{
public:
    explicit TileSharedReadGuard(const ImageDocument *doc)
        : m_doc(doc)
    {
        if (m_doc)
            TileCache::instance().beginSharedRead(m_doc);
    }
    ~TileSharedReadGuard()
    {
        if (m_doc)
            TileCache::instance().endSharedRead(m_doc);
    }
    TileSharedReadGuard(const TileSharedReadGuard &) = delete;
    TileSharedReadGuard &operator=(const TileSharedReadGuard &) = delete;

private:
    const ImageDocument *m_doc = nullptr;
};

/** RAII：钉住一个 TileBuffer。 */
class TilePinGuard
{
public:
    explicit TilePinGuard(const TileBuffer *b)
        : m_b(b)
    {
        if (m_b)
            TileCache::instance().pin(m_b);
    }
    ~TilePinGuard()
    {
        if (m_b)
            TileCache::instance().unpin(m_b);
    }
    TilePinGuard(const TilePinGuard &) = delete;
    TilePinGuard &operator=(const TilePinGuard &) = delete;

private:
    const TileBuffer *m_b = nullptr;
};

} // namespace Ps

#endif // ENGINE_CACHE_TILECACHE_H
