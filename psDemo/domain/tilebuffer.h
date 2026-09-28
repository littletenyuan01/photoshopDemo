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
 * 【功能】一层像素的真正容器：不整层 malloc，只在被写到的格子上分配 QImage。
 *
 * 【关键约定】
 * - 创建时只记 extent（宽高）与格数，**不**分配整层像素。
 * - ensureTile：第一次碰到某格才分配该块（边缘块可小于 64）。
 * - 透明层 hash 为空 ≈ 逻辑全透明；无全局共享 zero-tile / scratch。
 * - 像素格式固定为 Format_ARGB32_Premultiplied。
 *
 * 【对照】GIMP GeglBuffer 瓦片语义；本 Demo 去掉 COW / 交换 / 全局空瓦片池。
 * 详见 docs/layers/tiles-and-memory.md。
 */
class TileBuffer
{
public:
    /** 单瓦片边长（像素）；与 GIMP 默认瓦片尺寸同量级，取 64 便于心算。 */
    static constexpr int kTileSize = 64;

    TileBuffer() = default;
    /** 预定 extent；不分配任何瓦片块。 */
    TileBuffer(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    /** 横向 / 纵向瓦片格数（含边缘不足 64 的一格）。 */
    int tilesX() const { return m_tilesX; }
    int tilesY() const { return m_tilesY; }
    /** 已分配的瓦片块数（透明新建层为 0）。 */
    int allocatedTileCount() const { return m_tiles.size(); }
    /** hash 为空：逻辑全透明，尚未写过任何像素。 */
    bool isEmpty() const { return m_tiles.isEmpty(); }

    /**
     * 重置 extent 并清空全部瓦片。
     * 用于打开图片、画布/图像大小改尺寸后重建缓冲。
     */
    void reset(int width, int height);
    /** 只释放已分配瓦片，extent（宽高/格数）不变——等价「整层变透明」。 */
    void clearTiles();

    /**
     * 确保 (tx,ty) 有独立像素块；越界返回 nullptr。
     * 新分配块填透明（预乘全 0）。画笔首次碰到该格时走这里。
     */
    QImage *ensureTile(int tx, int ty);
    /** 已存在则返回，否则 nullptr（**不**分配）——合成只读路径用。 */
    QImage *tileAt(int tx, int ty);
    const QImage *tileAt(int tx, int ty) const;

    /** 该瓦片在图像坐标中的矩形（右/下边缘可小于 kTileSize）。 */
    QRect tileBounds(int tx, int ty) const;

    /**
     * 整缓冲填充。
     * 全透明 → clearTiles（释放内存）；否则 ensure 覆盖范围内全部瓦片并填色。
     */
    void fill(const QColor &color);

    /**
     * 用整图替换缓冲内容：先 reset 到图像尺寸，再按格拆入。
     * 打开图片、图层 replaceFromImage（图像大小/画布大小）走这里。
     */
    void setFromImage(const QImage &image);

    /**
     * 拼成整层临时图（缩略图、重采样用）。
     * 无瓦片时返回全透明同尺寸图，**不会**为此分配持久瓦片。
     */
    QImage materialize() const;

    using TileCallback = std::function<void(int tx, int ty, QImage &tile, const QRect &bounds)>;
    using ConstTileCallback = std::function<void(int tx, int ty, const QImage &tile, const QRect &bounds)>;

    /**
     * 遍历与 rect 相交的瓦片格。
     * @param allocateMissing true → ensureTile（绘制写路径）；false → 只走已分配块（合成读路径）
     */
    void forEachTileInRect(const QRect &rect, bool allocateMissing, const TileCallback &fn);
    /** 只遍历已分配瓦片（materialize / 缩略图）。 */
    void forEachAllocatedTile(const ConstTileCallback &fn) const;

private:
    /** 把 (tx,ty) 编成 hash 键；要求 m_tilesX > 0。 */
    qint64 key(int tx, int ty) const { return qint64(ty) * m_tilesX + tx; }
    bool validTileIndex(int tx, int ty) const;

    int m_width = 0;   ///< 逻辑宽度（像素）
    int m_height = 0;  ///< 逻辑高度（像素）
    int m_tilesX = 0;  ///< 横向格数
    int m_tilesY = 0;  ///< 纵向格数
    /** 稀疏存储：键 = ty*tilesX+tx，值 = 该格预乘 ARGB 图。 */
    QHash<qint64, QImage> m_tiles;
};

} // namespace Ps

#endif // TILEBUFFER_H
