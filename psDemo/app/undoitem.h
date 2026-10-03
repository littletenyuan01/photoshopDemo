/**
 * undoitem.h — 撤销条目类型与图层/文档快照（app 层）。
 *
 * 各 UndoItem 子类通过 pop() 与 undo/redo 对称交换状态；由 HistoryStack 调度。
 * 对照 GIMP：核心 mutate API 内 push（非独立 Command 总线）。
 */
#ifndef UNDOITEM_H
#define UNDOITEM_H

#include "domain/blendmode.h"
#include "domain/filternode.h"
#include "domain/layerstyle.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <memory>
#include <vector>

namespace Ps {

class ImageDocument;
class Layer;

/**
 * 单条撤销记录（对照 GIMP GimpUndo）。
 * pop = undo/redo 对称交换。
 */
class UndoItem
{
public:
    virtual ~UndoItem() = default;
    /** 菜单/历史面板显示的条目名称。 */
    virtual QString name() const = 0;
    /** 条目占用内存估算（用于 HistoryStack 字节上限 trim）。 */
    virtual quint64 byteSize() const = 0;
    /** undo/redo 对称交换：将文档状态与本条目保存的状态互换。 */
    virtual void pop(ImageDocument &doc) = 0;
};

/**
 * 图层属性快照（含样式/滤镜栈；不含像素）。
 * 用于属性 undo 与 DocumentGeomUndo。
 */
struct LayerPropSnapshot {
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    int offsetX = 0;
    int offsetY = 0;
    QVector<LayerStyleEffect> styles;
    QVector<FilterNode> filters;
    bool hasMask = false;
    bool maskEnabled = true;
    bool maskLinked = true;
    QImage maskGray; ///< Format_Grayscale8；hasMask 时有效
};

/** 从 Layer 读取当前属性到快照。 */
LayerPropSnapshot captureLayerProps(const Layer &layer);
/** 将快照写回 Layer（不触发 undo）。 */
void applyLayerProps(Layer &layer, const LayerPropSnapshot &s);

/**
 * 图层像素 undo：pop 时与当前 materialize / offset 交换。
 * 存 offset：自由变换扩层后撤销能恢复几何。
 */
class LayerPixelsUndo final : public UndoItem
{
public:
    LayerPixelsUndo(int layerIndex, QImage pixels, int offsetX, int offsetY,
                    const QString &label);
    /**
     * 像素 + 蒙版一体快照（应用蒙版：烘焙后删除蒙版，撤销需同时恢复）。
     */
    LayerPixelsUndo(int layerIndex, QImage pixels, int offsetX, int offsetY,
                    bool hasMask, bool maskEnabled, bool maskLinked, QImage maskGray,
                    const QString &label);

    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1;
    int m_offsetX = 0;
    int m_offsetY = 0;
    QImage m_pixels;
    bool m_trackMask = false;
    bool m_hasMask = false;
    bool m_maskEnabled = true;
    bool m_maskLinked = true;
    QImage m_maskGray;
    QString m_label;
};

/** 图层属性 undo：pop 时与当前属性（含样式/滤镜）交换。 */
class LayerPropUndo final : public UndoItem
{
public:
    LayerPropUndo(int layerIndex, LayerPropSnapshot before, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1;
    LayerPropSnapshot m_snap;
    QString m_label;
};

/**
 * 图层结构 undo：增/删层对称切换。
 * pop 时 Added↔Removed 翻转，并恢复/移除 Layer 与活动层索引。
 */
class LayerStructureUndo final : public UndoItem
{
public:
    enum class Kind { Added, Removed };

    static std::unique_ptr<LayerStructureUndo> forAdded(int index, const QString &label);
    static std::unique_ptr<LayerStructureUndo> forRemoved(int index,
                                                         std::unique_ptr<Layer> layer,
                                                         int activeIndexAfter,
                                                         const QString &label);

    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    LayerStructureUndo() = default;

    Kind m_kind = Kind::Added;
    int m_index = -1;
    int m_activeIndex = -1;
    std::unique_ptr<Layer> m_layer;
    QString m_label;
};

/**
 * 选区 mask undo（对照 GIMP channel/selection undo 精简）。
 * pop 时与当前选区 mask 交换。
 */
class SelectionUndo final : public UndoItem
{
public:
    SelectionUndo(QImage mask, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    QImage m_mask;
    QString m_label;
};

/**
 * 文档几何 undo：画布尺寸/选区/全层像素与属性整体交换。
 * 用于 resizeCanvas、scaleImage 等整文档变更。
 */
class DocumentGeomUndo final : public UndoItem
{
public:
    struct LayerState {
        LayerPropSnapshot props;
        QImage pixels;
        int width = 0;
        int height = 0;
    };

    DocumentGeomUndo(int width, int height, QImage selectionMask,
                     std::vector<LayerState> layers, int activeIndex,
                     const QString &label);

    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    int m_width = 0;
    int m_height = 0;
    int m_activeIndex = -1;
    QImage m_selectionMask;
    std::vector<LayerState> m_layers;
    QString m_label;
};

} // namespace Ps

#endif // UNDOITEM_H
