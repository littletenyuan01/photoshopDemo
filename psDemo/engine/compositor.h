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
};

} // namespace Ps

#endif // COMPOSITOR_H
