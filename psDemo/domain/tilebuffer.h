#ifndef TILEBUFFER_H
#define TILEBUFFER_H

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
 * - 创建时只记 extent 与格数，**不**分配整层像素。
 * - ensureTile：第一次碰到某格才分配该块 QImage（边缘块可小于 64）。
 * - 透明层 hash 为空 ≈ 逻辑全透明；无全局共享 zero-tile / scratch。
 */
class TileBuffer
{
public:
    static constexpr int kTileSize = 64;

    TileBuffer() = default;
    TileBuffer(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    /** 已分配的瓦片块数（透明新建层为 0）。 */
    int allocatedTileCount() const { return m_tiles.size(); }
    bool isEmpty() const { return m_tiles.isEmpty(); }

    void reset(int width, int height);
    void clearTiles();

    /**
     * 确保 (tx,ty) 有独立像素块；越界返回 nullptr。
     * 新分配块填透明（预乘全 0）。
     */
    QImage *ensureTile(int tx, int ty);
    /** 已存在则返回，否则 nullptr（不分配）。 */
    QImage *tileAt(int tx, int ty);
    const QImage *tileAt(int tx, int ty) const;

    /** 该瓦片在图像坐标中的矩形（边缘可小于 kTileSize）。 */
    QRect tileBounds(int tx, int ty) const;

    /**
     * 整缓冲填充。全透明 → clearTiles；否则确保覆盖范围内全部瓦片并填色。
     */
    void fill(const QColor &color);

    /** 从整图拆入瓦片（打开图片）；先清空再写入非空块。 */
    void setFromImage(const QImage &image);

    /** 拼成整层临时图（缩略图等）；无瓦片时返回全透明同尺寸图。 */
    QImage materialize() const;

    using TileCallback = std::function<void(int tx, int ty, QImage &tile, const QRect &bounds)>;
    using ConstTileCallback = std::function<void(int tx, int ty, const QImage &tile, const QRect &bounds)>;

    /**
     * 遍历与 rect 相交的瓦片格。
     * @param allocateMissing true 时对相交格 ensureTile（绘制）；false 只走已分配块（合成）。
     */
    void forEachTileInRect(const QRect &rect, bool allocateMissing, const TileCallback &fn);
    void forEachAllocatedTile(const ConstTileCallback &fn) const;

private:
    qint64 key(int tx, int ty) const { return qint64(ty) * m_tilesX + tx; }
    bool validTileIndex(int tx, int ty) const;

    int m_width = 0;
    int m_height = 0;
    int m_tilesX = 0;
    int m_tilesY = 0;
    QHash<qint64, QImage> m_tiles;
};

} // namespace Ps

#endif // TILEBUFFER_H
