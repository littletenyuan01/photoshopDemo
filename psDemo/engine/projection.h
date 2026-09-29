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
 */
class Projection
{
public:
    static constexpr int kChunkSize = 64;

    void bind(ImageDocument *document);

    ImageDocument *document() const { return m_document; }

    /**
     * 按文档脏区刷新投影。
     * @return 本轮实际重算的文档矩形；无需更新时为空。
     */
    QRect sync();

    const QImage &image() const { return m_buffer; }

    bool isNull() const { return m_buffer.isNull(); }

    void invalidate();

private:
    void resetChunkGrid(const QSize &pixelSize);
    void invalidateChunks(const QRect &pixelRect);
    void markChunksValid(const QRect &pixelRect);

    ImageDocument *m_document = nullptr;
    QImage m_buffer;
    QBitArray m_chunkValid; ///< 行主序：row * cols + col
    int m_chunkCols = 0;
    int m_chunkRows = 0;
};

} // namespace Ps

#endif // ENGINE_PROJECTION_H
