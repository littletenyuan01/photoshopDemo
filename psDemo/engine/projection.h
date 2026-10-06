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
 * 【渐进绘制】大脏区不一次合成完：sync() 先吃掉视口内块，其余挂起由
 * processPendingChunks() 在空闲定时器里分批做（对照 gimp_projection_chunk_render_*）。
 */
class Projection
{
public:
    static constexpr int kChunkSize = 64;
    /** sync() 单次最多立即合成的块数（视口内不计入此上限）。 */
    static constexpr int kSyncChunkBudget = 32;
    /** 空闲 tick 每批合成块数。 */
    static constexpr int kIdleChunkBudget = 12;

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
     * 吸入文档脏区并尽量立即刷新（视口优先）。
     * @return 本轮实际重算的文档矩形；无需更新时为空。
     */
    QRect sync();

    /**
     * 空闲分块：再合成最多 @p maxChunks 个无效块。
     * @return 本批重算并集；无待办为空。
     */
    QRect processPendingChunks(int maxChunks = kIdleChunkBudget);

    /** 是否还有未合成的无效块。 */
    bool hasPendingChunks() const;

    /** 当前投影缓冲（预乘 ARGB32，文档尺寸）。 */
    const QImage &image() const { return m_buffer; }

    bool isNull() const { return m_buffer.isNull(); }

    /** 丢弃缓冲与块有效位图（文档尺寸变更或解绑时）。 */
    void invalidate();

private:
    void resetChunkGrid(const QSize &pixelSize);
    void invalidateChunks(const QRect &pixelRect);
    void markChunksValid(const QRect &pixelRect);
    bool ensureBuffer(const QSize &pixelSize);
    QRect chunkRectAt(int col, int row, const QRect &full) const;
    int invalidChunkCount() const;
    /**
     * 合成无效块。
     * @param priorityFirst 先处理与 m_priorityRect 相交的块
     * @param maxChunks ≤0 表示不限
     */
    QRect recomposeInvalidChunks(bool priorityFirst, int maxChunks);

    ImageDocument *m_document = nullptr;
    QImage m_buffer;          ///< 投影缓存（预乘 ARGB32）
    QBitArray m_chunkValid;   ///< 64×64 块有效位图，行主序：row * cols + col
    int m_chunkCols = 0;      ///< 块网格列数
    int m_chunkRows = 0;      ///< 块网格行数
    QRect m_priorityRect;     ///< 视口优先区（文档坐标）
};

} // namespace Ps

#endif // ENGINE_PROJECTION_H
