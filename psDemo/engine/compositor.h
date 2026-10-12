/**
 * compositor.h — 图层合成器（engine 层）。
 *
 * 只读文档、输出投影预览；blend（颜色）与 composite_union（Alpha）分工。
 * 对照 GIMP layer-modes composite；Phase 7 支持 compositeRegion 脏区就地更新。
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <QImage>
#include <QRect>

namespace Ps {

class ImageDocument;

/**
 * 图层合成器（engine）。
 * 只读文档，输出投影预览；不修改各层 pixels。
 * 自底向顶混合；对照 GIMP layer-modes。透明底由 Canvas 画棋盘格。
 *
 * Phase 7：支持按脏矩形就地更新已有投影缓冲（compositeRegion）。
 */
class Compositor
{
public:
    /** 全图合成，返回新图像。 */
    static QImage composite(const ImageDocument &doc);

    /**
     * 只计算 @p rect 内像素；返回整图尺寸，区外为透明
     * （适合导出局部预览；画布增量更新请用 compositeRegion）。
     */
    static QImage composite(const ImageDocument &doc, const QRect &rect);

    /**
     * 就地重算 @p dst 上的脏区：先清空该矩形再叠层。
     * @p dst 须为文档尺寸、ARGB32_Premultiplied；不匹配则返回 false。
     */
    static bool compositeRegion(QImage &dst, const ImageDocument &doc, const QRect &rect);

    /**
     * 把图层下标 [@p layerBegin, @p layerEnd) 叠到 @p dst（不先清空）。
     * 供自由变换合成预览：下层 → 浮层预览 → 上层，避免预览盖住全部图层。
     * @p skipLayer 若 ≥0 则跳过该层（变换中已挖空的层）。
     */
    static bool blendLayerRange(QImage &dst,
                                const ImageDocument &doc,
                                const QRect &rect,
                                int layerBegin,
                                int layerEnd,
                                int skipLayer = -1);

    /**
     * `blendLayerRange` 的**分块并行**版：把 @p rect 切成 64x64 块交给 QtConcurrent。
     *
     * 与 `blendLayerRange` **逐像素等价**（各块只写自己那块 dst：栅格层读瓦片；
     * 调整层点算子读本块下方结果；面积算子在**私有子缓冲**里重叠下方外扩区，只写回本块）。
     *
     * 用途：工具层「拖中逐帧」的单次大区域重合成 —— 例如移动层上方有调整层时，
     * 每帧都要重跑上方全部层；实测 2480x3508 文档上该调用串行需 59~64ms/帧（约 16fps）。
     *
     * 内部会在**主线程**先预热各层 mutable 缓存并确保 @p dst 独占。
     */
    static bool blendLayerRangeParallel(QImage &dst,
                                        const ImageDocument &doc,
                                        const QRect &rect,
                                        int layerBegin,
                                        int layerEnd,
                                        int skipLayer = -1);

    /**
     * 并行合成前在**主线程**预热各层的 mutable 缓存
     * （`Layer::ensureCompositeRaster` / `LayerMask::isFullyOpaque`）。
     * 不预热 → worker 首访会写这两个缓存 → 数据竞争。
     */
    static void warmLayerCaches(const ImageDocument &doc);

    /**
     * 经图引擎合成（对照 composite 的图化等价路径，Stage 3）。
     * 构建合成 DAG 并对 @p rect 求值；返回整图尺寸（区外透明）。
     * 供校验图引擎与直接合成逐像素一致（见 graphselfcheck）。
     */
    static QImage compositeViaGraph(const ImageDocument &doc, const QRect &rect);
};

} // namespace Ps

#endif // COMPOSITOR_H
