/**
 * tilebuffer.h — 64×64 稀疏瓦片像素缓冲（domain 层）。
 *
 * 透明层不分配瓦片；首次写入或 fill 才 ensureTile；格式 ARGB32 预乘。
 * 可选接入 TileCache 换页（默认关闭，见 docs/page-cache-plan.md）。
 */
#ifndef TILEBUFFER_H
#define TILEBUFFER_H

#include "engine/cache/tilecache.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QtGlobal>

#include <functional>

namespace Ps {

/**
 * 按 64×64 网格存放图层像素（对照 GIMP/GEGL 瓦片缓冲的瘦身版）。
 *
 * 【功能】一层像素的真正容器：不整层 malloc，只在被写到的格子上分配。
 *
 * 【关键约定】
 * - 创建时只记 extent（宽高）与格数，**不**分配整层像素。
 * - ensureTile：第一次碰到某格才分配该块（边缘块可小于 64）。
 * - 透明层 hash 为空 ≈ 逻辑全透明；无全局共享 zero-tile / scratch。
 * - 像素格式固定为 Format_ARGB32_Premultiplied。
 * - 换页开启时，槽可为「仅在 swap」；ensureTile/tileAt 会按需读回。
 *
 * 【对照】GIMP GeglBuffer 瓦片语义；本 Demo 可选交换（默认关），不做 per-tile ref_count。
 */
class TileBuffer
{
public:
    static constexpr int kTileSize = 64;

    TileBuffer();
    explicit TileBuffer(int width, int height);
    ~TileBuffer();

    TileBuffer(const TileBuffer &) = delete;
    TileBuffer &operator=(const TileBuffer &) = delete;
    TileBuffer(TileBuffer &&other) noexcept;
    TileBuffer &operator=(TileBuffer &&other) noexcept;

    int width() const { return m_width; }
    int height() const { return m_height; }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    /** 已分配槽数（含仅在 swap、当前不在内存的）。 */
    int allocatedTileCount() const { return m_tiles.size(); }
    bool isEmpty() const { return m_tiles.isEmpty(); }

    /** 当前驻留内存的瓦片字节合计（边缘按实际宽高）。 */
    qint64 residentBytes() const;
    /** 单槽驻留字节；img null 时为 0。 */
    qint64 slotResidentBytes(const TileSlot &slot) const;

    void reset(int width, int height);
    void clearTiles();

    /**
     * 从另一缓冲**共享**像素（QImage 隐式共享），不走 materialize。
     * 复制图层用：内存增量应远小于整层（P4 检查点）。
     * 会先把源侧 swap 中的瓦片读回再共享。
     */
    void sharePixelsFrom(const TileBuffer &other);

    QImage *ensureTile(int tx, int ty);
    QImage *tileAt(int tx, int ty);
    const QImage *tileAt(int tx, int ty) const;

    QRect tileBounds(int tx, int ty) const;

    void fill(const QColor &color);
    void setFromImage(const QImage &image);
    QImage materialize() const;

    using TileCallback = std::function<void(int tx, int ty, QImage &tile, const QRect &bounds)>;
    using ConstTileCallback = std::function<void(int tx, int ty, const QImage &tile, const QRect &bounds)>;

    void forEachTileInRect(const QRect &rect, bool allocateMissing, const TileCallback &fn);
    void forEachTileInRect(const QRect &rect, const ConstTileCallback &fn) const;
    void forEachAllocatedTile(const ConstTileCallback &fn) const;

    /** 供 TileCache LRU 扫描：只访问当前驻留内存的槽。 */
    void visitResidentSlots(const std::function<void(qint64 key, TileSlot &slot)> &fn);

    /**
     * 瓦片引用快照：只保存 QImage 的**共享引用**（隐式共享），不做像素深拷贝。
     * images 里没有的 key = 当时该格「未分配」，语义为全透明。
     * tilesX/tilesY/width/height 用于交换前校验网格一致（不一致必须拒绝）。
     */
    struct TileRefSnapshot {
        int width = 0;
        int height = 0;
        int tilesX = 0;
        int tilesY = 0;
        QHash<qint64, QImage> images;
        /** 快照当前持有的像素字节（含边缘块实际尺寸），供撤销条目记账。 */
        qint64 bytes() const;
    };

    /**
     * 取引用快照。会把「仅在 swap」的瓦片读回内存（换页开启时才有代价；
     * 默认换页关闭 ⇒ 无代价）。
     */
    TileRefSnapshot takeRefSnapshot();

    /**
     * 与快照做双向交换（undo/redo 同一入口）：
     *  · 两边都有的 key：交换 QImage（O(1) 引用交换）
     *  · 只有快照有的 key：层内新建透明格后交换（= 恢复旧格）
     *  · 只有层有的 key：把该 QImage 交给快照并从层内删除（= 回到「未分配」）
     * 网格不一致时**不做任何改动**并返回 false。
     */
    bool swapWithRefSnapshot(TileRefSnapshot &snap);

    /** 把当前持有像素的全透明格删除（预留；本计划不调用）。 */
    void eraseFullyTransparentTiles();

private:
    friend class TileCache;

    qint64 key(int tx, int ty) const { return qint64(ty) * m_tilesX + tx; }
    bool validTileIndex(int tx, int ty) const;
    QImage *makeResident(qint64 k, TileSlot &slot, int tx, int ty);
    const QImage *makeResidentConst(qint64 k, TileSlot &slot, int tx, int ty) const;

    int m_width = 0;
    int m_height = 0;
    int m_tilesX = 0;
    int m_tilesY = 0;
    QHash<qint64, TileSlot> m_tiles;
};

} // namespace Ps

#endif // TILEBUFFER_H
