/**
 * layerstyleeval.h — 图层样式求值（engine 层）。
 *
 * 输入层像素临时图，输出带 fx 的更大图 + 相对层原点的偏移。
 * 对照 GIMP drawable filter 非破坏求值；算法近似 gegl:dropshadow / color-overlay。
 */
#ifndef ENGINE_LAYERSTYLEEVAL_H
#define ENGINE_LAYERSTYLEEVAL_H

#include <QImage>
#include <QPoint>

namespace Ps {

class LayerStyleStack;

struct StyledLayerResult {
    QImage image; ///< ARGB32_Premultiplied
    int originDx = 0; ///< 相对层原点：image(0,0) 在文档中 = layerOx + originDx
    int originDy = 0;
};

class LayerStyleEval
{
public:
    /**
     * 对 @p source（层局部像素，任意格式）应用样式栈。
     * 无启用样式时返回 source 转预乘、origin=0。
     */
    static StyledLayerResult apply(const QImage &source, const LayerStyleStack &styles);
};

} // namespace Ps

#endif // ENGINE_LAYERSTYLEEVAL_H
