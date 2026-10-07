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
     * 吸入文档脏区并尽量立即刷新（视口优先、有预算）。
     * @return 本轮实际重算的文档矩形；无需更新时为空。
     */
    QRect sync();

    /**
     * 空闲分块：再合成最多 @p maxChunks 个无效块（仍视口优先）。
     * @return 本批重算并集；无待办为空。
     */
    QRect processPendingChunks(int maxChunks = kIdleChunkBudget);

    /**
     * 立刻刷完视口优先区内全部无效块（无预算）。
     * 仅用于整幅脏（显隐调整层等），避免画出「旧块+新块」拼贴。
     */
    QRect flushPriority();

    /** 是否还有未合成的无效块。 */
    bool hasPendingChunks() const;

    /** 当前投影缓冲（预乘 ARGB32，文档尺寸）。 */
    const QImage &image() const { return m_buffer; }

    bool isNull() const { return m_buffer.isNull(); }

    /** 丢弃缓冲与块有效位图（文档尺寸变更或解绑时）。 */
    void invalidate();

    /**
     * 用整幅图像替换投影缓冲并标全部块有效。
     * 供移动工具松手时把 live 无缝交给投影，避免「先消失再重算」。
     */
    bool adoptImage(const QImage &image);

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
     */
    QRect recomposeInvalidChunks(int maxChunks, ScanMode mode);

    ImageDocument *m_document = nullptr;
    QImage m_buffer;          ///< 投影缓存（预乘 ARGB32）
    QBitArray m_chunkValid;   ///< 64×64 块有效位图，行主序：row * cols + col
    int m_chunkCols = 0;      ///< 块网格列数
    int m_chunkRows = 0;      ///< 块网格行数
    QRect m_priorityRect;     ///< 视口优先区（文档坐标）
};

} // namespace Ps

#endif // ENGINE_PROJECTION_H
