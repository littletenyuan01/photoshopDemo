#ifndef SELECTION_H
#define SELECTION_H

#include <QImage>
#include <QRect>

namespace Ps {

/**
 * 选区运算模式（对照 GIMP GimpChannelOps）。
 * 矩形/椭圆等工具写入同一张文档级 mask 时用这些模式组合。
 */
enum class ChannelOp {
    Add = 0,       ///< GIMP_CHANNEL_OP_ADD
    Subtract = 1,  ///< GIMP_CHANNEL_OP_SUBTRACT
    Replace = 2,   ///< GIMP_CHANNEL_OP_REPLACE
    Intersect = 3, ///< GIMP_CHANNEL_OP_INTERSECT
};

/**
 * 文档级选区 mask（对照 GIMP GimpSelection / selection_mask）。
 *
 * - 归属 ImageDocument，与文档同生命周期；不是图层字段。
 * - 坐标系 = 文档像素；选中 ≈ 255，未选中 = 0（Format_Grayscale8）。
 * - 矩形/椭圆/套索只是写入方式不同；持久真相始终是这一张 mask。
 */
class Selection
{
public:
    Selection() = default;
    Selection(int width, int height);

    int width() const { return m_mask.width(); }
    int height() const { return m_mask.height(); }

    /** 重建为空 mask（尺寸变化时由 ImageDocument 调用）。 */
    void reset(int width, int height);

    /**
     * 画布大小变更：旧 mask 按锚点偏移贴入新尺寸（对照图层 resizeCanvas）。
     * 扩大处为未选中(0)；裁掉的部分丢失。
     */
    void resizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY);

    /** 图像大小（重采样）：最近邻缩放 mask。 */
    void scale(int newWidth, int newHeight);

    bool isEmpty() const;
    /** 非零像素的外接矩形；空选区返回空矩形。 */
    QRect bounds() const;

    const QImage &mask() const { return m_mask; }

    /** 文档坐标取值；越界视为 0。 */
    quint8 value(int docX, int docY) const;
    bool isSelected(int docX, int docY) const { return value(docX, docY) > 0; }

    void clear();
    void selectAll();
    void invert();

    /**
     * 用灰度图整体替换 mask（工程文件加载用）。
     * 会缩放到当前文档尺寸；非 Grayscale8 时先转换。
     */
    void replaceFromImage(const QImage &mask);

    /**
     * 矩形选区写入（对照 gimp_channel_select_rectangle → gimp_channel_combine_rect）。
     * @param rect 文档坐标；与画布求交后写入；空矩形且 Replace 则清空。
     */
    void selectRectangle(const QRect &rect, ChannelOp op);

    /**
     * 椭圆选区写入（对照 gimp_channel_select_ellipse → gimp_channel_combine_ellipse）。
     * 椭圆内接于 @p rect；本项目不做 antialias / feather（硬边）。
     */
    void selectEllipse(const QRect &rect, ChannelOp op);

    /**
     * 由图层 alpha 建立选区（对照 gimp_channel_select_alpha）。
     * @param layerPremul 层像素（预乘 ARGB）；@param offsetX/Y 层在文档中的偏移
     * 将 alpha 写入文档坐标系 mask（透明=未选，不透明=选中；中间值保留作软边）。
     */
    void selectFromLayerAlpha(const QImage &layerPremul,
                              int offsetX, int offsetY,
                              ChannelOp op);

private:
    void invalidateCache() const;
    void recomputeCache() const;
    void fillRect(const QRect &rect, quint8 value);
    /** 按 ChannelOp 把 shapeMask（同尺寸灰度，形状内=255）合并进 m_mask。 */
    void combineShapeMask(const QImage &shapeMask, ChannelOp op, const QRect &boundsHint);

    QImage m_mask; ///< Format_Grayscale8；文档尺寸

    mutable bool m_cacheValid = false;
    mutable bool m_empty = true;
    mutable QRect m_bounds;
};

} // namespace Ps

#endif // SELECTION_H
