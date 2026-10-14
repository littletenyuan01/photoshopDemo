/**
 * projection.h — 文档投影缓存（engine 层）。
 *
 * 只读合成结果供 CanvasView 绘制；64×64 块有效位图 + 脏区对齐增量 sync。
 * 对照 GIMP GimpProjection：chunk idle 渐进绘制 + priority_rect 视口优先。
 */
#ifndef ENGINE_PROJECTION_H
#define ENGINE_PROJECTION_H

#include <QBitArray>
#include <QImage>
#include <QRect>
#include <QSize>

namespace Ps {

class ImageDocument;

/**
 * 文档投影缓存（对照 GimpProjection）。
 *
 * 文档是像素真相；本类是只读合成结果，供 CanvasView 绘制。
 * 按 64×64 块维护有效位图：脏区对齐到块后只重算失效块。
 *
 * 【渐进绘制】大脏区不一次合成完：sync() 先限额刷视口内块，其余挂起由
 * processPendingChunks() 在空闲定时器里分批做（对照 gimp_projection_chunk_render_*）。
 */
class Projection
{
public:
    static constexpr int kChunkSize = 64;
    /** sync() 视口内本帧最多立即合成的块数（大图拖层时避免一帧刷几百块）。 */
    static constexpr int kSyncPriorityBudget = 48;
    /** sync() 视口外本帧最多再合成的块数。 */
    static constexpr int kSyncChunkBudget = 24;
    /** 空闲 tick 每批合成块数。 */
    static constexpr int kIdleChunkBudget = 16;

    /** 绑定文档；换文档时 invalidate 投影缓冲。 */
    void bind(ImageDocument *document);

    ImageDocument *document() const { return m_document; }

    /**
     * 视口优先矩形（文档坐标）。
     * 对照 gimp_projection_set_priority_rect：无效块先刷这里。
     */
    void setPriorityRect(const QRect &docRect);
    QRect priorityRect() const { return m_priorityRect; }

    /**
     * 吸入文档脏区并尽量立即刷新（视口优先、块数 + 时间双预算）。
     * @param maxMs 每阶段最多耗时（毫秒）；≤0 = 不限。给预算可避免大脏区一帧卡住 UI。
     * @return 本轮实际重算的文档矩形；无需更新时为空。
     */
    QRect sync(int maxMs = -1);

    /**
     * 空闲分块：再合成最多 @p maxChunks 个无效块（仍视口优先）。
     * @param maxMs 最多耗时（毫秒）；≤0 = 不限。
     * @return 本批重算并集；无待办为空。
     */
    QRect processPendingChunks(int maxChunks = kIdleChunkBudget, int maxMs = -1);

    /**
     * 刷视口优先区内的无效块。
     * @param maxMs 最多耗时（毫秒）；≤0 = 不限。
     * 仅用于整幅脏（显隐调整层等），避免画出「旧块+新块」拼贴；
     * 调用方须给预算，否则大视口会一次同步刷完（实测 ~170ms）。
     */
    QRect flushPriority(int maxMs = -1);

    /** 是否还有未合成的无效块。 */
    bool hasPendingChunks() const;

    /** 当前投影缓冲（预乘 ARGB32，文档尺寸）。 */
    const QImage &image() const { return m_buffer; }

    bool isNull() const { return m_buffer.isNull(); }

    /** 丢弃缓冲与块有效位图（文档尺寸变更或解绑时）。 */
    void invalidate();

    /**
     * 仅把全部块标为无效（保留投影像素缓冲）。
     * 页缓存压力下淘汰「可重算」派生缓存时用；下次 sync 会重算。
     */
    void invalidateChunkValidity();

    /**
     * 用图像替换投影缓冲。尺寸须等于文档。
     * @param coveredDoc 已覆盖的文档区域；空或盖住整幅 → 全部块有效；
     *        否则仅 covered 内块有效，其余保持无效以便 idle 重算（A32）。
     * 供移动/变换松手时把 live 无缝交给投影，避免「先消失再重算」。
     */
    bool adoptImage(const QImage &image, const QRect &coveredDoc = QRect());

    /** 投影缓冲占用字节（空则为 0）。 */
    qint64 bufferBytes() const;

private:
    enum class ScanMode {
        PriorityOnly,     ///< 只扫与 priority 相交的无效块
        NonPriorityOnly,  ///< 只扫视口外无效块
        PriorityThenRest  ///< 先视口再其余（共用同一 maxChunks 预算）
    };

    void resetChunkGrid(const QSize &pixelSize);
    void invalidateChunks(const QRect &pixelRect);
    void markChunksValid(const QRect &pixelRect);
    bool ensureBuffer(const QSize &pixelSize);
    QRect chunkRectAt(int col, int row, const QRect &full) const;
    int invalidChunkCount() const;
    /**
     * 合成无效块。
     * @param maxChunks ≤0 表示不限（仅用于「剩余很少」时一次收尾）
     * @param maxMs    最多耗时（毫秒）；≤0 = 不限。与 maxChunks 谁先到谁停。
     */
    QRect recomposeInvalidChunks(int maxChunks, ScanMode mode, int maxMs = -1);

    ImageDocument *m_document = nullptr;
    QImage m_buffer;          ///< 投影缓存（预乘 ARGB32）
    QBitArray m_chunkValid;   ///< 64×64 块有效位图，行主序：row * cols + col
    int m_chunkCols = 0;      ///< 块网格列数
    int m_chunkRows = 0;      ///< 块网格行数
    QRect m_priorityRect;     ///< 视口优先区（文档坐标）
    double m_msPerChunkParallel = 0.25; ///< 并行单块耗时估计（EMA；用于折算批大小）
};

} // namespace Ps

#endif // ENGINE_PROJECTION_H
