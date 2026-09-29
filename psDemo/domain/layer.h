/**
 * layer.h — 单图层：属性 + TileBuffer 像素 + FilterStack（domain 层）。
 *
 * 持 owner 回指 ImageDocument，属性 setter 内部自动广播；像素写入方须自行 markDirty。
 */
#ifndef LAYER_H
#define LAYER_H

#include "blendmode.h"
#include "filterstack.h"
#include "tilebuffer.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 单图层（domain 层）。
 *
 * 【功能】一层可编辑图像：属性（名/显隐/不透明度/混合）+ 像素像素（TileBuffer）。
 * 像素真相在 TileBuffer（64×64 懒分配）中；格式 ARGB32 预乘。
 * 对应 GIMP GimpLayer + GeglBuffer 瓦片语义的瘦身版：无组层；滤镜栈只读求值。
 *
 * 【变更通知】本类持有 owner 回指（由 ImageDocument 在入栈时设置）。
 * 属性 setter 内部改值后会通知 owner 发信号，因此 **UI 调用 setter 即自动刷新**。
 *
 * 【像素写入】经 tiles() 写瓦片；写入方**必须**自行调用 ImageDocument::markDirty(rect)。
 */
class Layer
{
public:
    /**
     * 创建透明层：只预定宽高与瓦片格数，**不**分配像素块
     * （对照 GIMP gegl_buffer_new(extent) + 空瓦片）。
     * 图层面板「新建」走这条路径。
     */
    Layer(const QString &name, int width, int height);
    /**
     * 用已有图像构造；内部 setFromImage 按块拆入 TileBuffer。
     * 「打开图片」建背景层走这条路径。
     */
    Layer(const QString &name, const QImage &pixels);

    QString name() const { return m_name; }
    /** 改图层显示名；值未变则忽略。会经 owner 发 layerPropertiesChanged。 */
    void setName(const QString &name);

    bool isVisible() const { return m_visible; }
    /** 改显隐；值未变则忽略。会经 owner 广播。 */
    void setVisible(bool visible);

    /** 不透明度 [0,1]。 */
    qreal opacity() const { return m_opacity; }
    /** 改不透明度（自动钳到 [0,1]）；值未变则忽略。 */
    void setOpacity(qreal opacity);

    BlendMode blendMode() const { return m_blendMode; }
    /** 改混合模式（PS 27 种，合成期生效；见 `engine/blend.cpp`）。 */
    void setBlendMode(BlendMode mode);

    /**
     * 图层在文档坐标系中的偏移（对照 GIMP GimpItem::offset_x/y）。
     * 移动工具改这两个值，不搬瓦片像素。
     */
    int offsetX() const { return m_offsetX; }
    int offsetY() const { return m_offsetY; }
    /** 设置绝对偏移；值未变则忽略。经 owner 发 layerPropertiesChanged。 */
    void setOffset(int x, int y);
    /**
     * 只写 offset、不发信号（图像/画布大小批量改尺寸时用）。
     * 调用方须自行 markDirty / structureChanged。
     */
    void setOffsetSilent(int x, int y);
    /** 相对平移（对照 gimp_item_translate 对层的增量）。 */
    void translate(int dx, int dy);

    /** 文档坐标 → 层内坐标（绘制/填充前换算）。 */
    QPointF toLayerLocal(const QPointF &imagePos) const;
    /** 层在文档中的外接矩形（offset + 像素宽高）。 */
    QRect boundsInDocument() const;

    /**
     * 非透明像素在文档中的最小外接矩形（对照 PS 变换控件 / Free Transform 框）。
     * 无像素或全透明 → 空矩形。结果按像素变更缓存，平移只改 offset 不重扫。
     */
    QRect contentBoundsInDocument() const;
    /** 像素写入后调用，使下次 contentBoundsInDocument 重算。 */
    void invalidateContentBounds() const;

    /**
     * 文档坐标处本层不透明度 [0,1]（对照 gimp_pickable_get_opacity_at）。
     * 点在层外、无瓦片或全透明 → 0。
     */
    qreal opacityAtDocumentPos(int docX, int docY) const;

    /** 像素缓冲读写入口；画笔/合成经此访问瓦片。 */
    TileBuffer &tiles() { return m_tiles; }
    const TileBuffer &tiles() const { return m_tiles; }

    /** 是否已有任意已分配瓦片（透明新建层为 false）。 */
    bool hasPixelData() const { return !m_tiles.isEmpty(); }

    /**
     * 图层滤镜栈（非破坏）。合成时对 materialize 结果求值，不写回 tiles。
     */
    FilterStack &filters() { return m_filters; }
    const FilterStack &filters() const { return m_filters; }

    /** 拼成整层临时图（缩略图 / 重采样）；无瓦片时为全透明同尺寸图。 */
    QImage materialize() const { return m_tiles.materialize(); }

    int width() const { return m_tiles.width(); }
    int height() const { return m_tiles.height(); }

    /**
     * 整层填充指定颜色。
     * 全透明 → 释放全部瓦片；实色 → 覆盖格均分配并填色。
     */
    void fill(const QColor &color);

    /**
     * 用整图替换本层像素与尺寸（图像大小 / 画布大小用）。
     * 内部走 TileBuffer::setFromImage；不发属性信号（由文档统一 markDirty / 发信号）。
     */
    void replaceFromImage(const QImage &pixels);

    /**
     * 挂文档 owner，使属性 setter 能回调广播。
     * **仅** ImageDocument::addLayer 应调用；UI 不要直接调。
     */
    void setOwner(ImageDocument *owner) { m_owner = owner; }

private:
    /** 属性变更后通知文档发 layerPropertiesChanged + contentChanged。 */
    void notifyPropertiesChanged();

    ImageDocument *m_owner = nullptr; ///< 入栈后回指文档；未入栈为 nullptr

    /** 扫描已分配瓦片，得到层内坐标的 alpha>0 包围盒。 */
    QRect computeContentBoundsLocal() const;

    QString m_name;          ///< 图层面板显示名
    bool m_visible = true;   ///< 是否参与合成
    qreal m_opacity = 1.0; ///< [0,1]
    BlendMode m_blendMode = BlendMode::Normal;
    int m_offsetX = 0; ///< 文档坐标 X（对照 GimpItem offset）
    int m_offsetY = 0; ///< 文档坐标 Y
    TileBuffer m_tiles; ///< 本层像素（懒分配瓦片；层内原点）
    FilterStack m_filters; ///< 非破坏滤镜节点（对照 drawable filter stack）

    /** 层内坐标内容包围盒缓存；与 offset 无关。 */
    mutable QRect m_contentBoundsLocal;
    mutable bool m_contentBoundsValid = false;
};

} // namespace Ps

#endif // LAYER_H
