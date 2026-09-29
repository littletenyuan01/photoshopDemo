/**
 * undoitem.h — 撤销条目类型与图层/文档快照（app 层）。
 *
 * 各 UndoItem 子类通过 pop() 与 undo/redo 对称交换状态；由 HistoryStack 调度。
 */
#ifndef UNDOITEM_H
#define UNDOITEM_H

#include "domain/blendmode.h"

#include <QImage>
#include <QString>
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

/** 图层属性快照（不含像素）；用于属性 undo 与 DocumentGeomUndo。 */
struct LayerPropSnapshot {
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    int offsetX = 0; ///< 文档坐标偏移
    int offsetY = 0;
};

/** 从 Layer 读取当前属性到快照。 */
LayerPropSnapshot captureLayerProps(const Layer &layer);
/** 将快照写回 Layer（不触发 undo）。 */
void applyLayerProps(Layer &layer, const LayerPropSnapshot &s);

/** 图层像素 undo：pop 时与当前 materialize 结果交换。 */
class LayerPixelsUndo final : public UndoItem
{
public:
    LayerPixelsUndo(int layerIndex, QImage pixels, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1; ///< 目标层在 LayerStack 中的索引
    QImage m_pixels;       ///< 交换用像素缓冲（文档尺寸）
    QString m_label;
};

/** 图层属性 undo：pop 时与当前属性交换。 */
class LayerPropUndo final : public UndoItem
{
public:
    LayerPropUndo(int layerIndex, LayerPropSnapshot before, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override { return sizeof(*this) + quint64(m_snap.name.size()) * 2; }
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1;
    LayerPropSnapshot m_snap; ///< pop 前保存的旧属性
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

    /** 新建层后的 undo 条目（pop = 删除该层）。 */
    static std::unique_ptr<LayerStructureUndo> forAdded(int index, const QString &label);
    /** 删除层前的 undo 条目（pop = 还原该层）。 */
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
    int m_index = -1;                    ///< 层在栈中的索引
    int m_activeIndex = -1;              ///< Removed 时记录的活动层
    std::unique_ptr<Layer> m_layer;      ///< Removed 时持有的被删层
    QString m_label;
};

/**
 * 文档几何 undo：画布尺寸/选区/全层像素与属性整体交换。
 * 用于 resizeCanvas、scaleImage 等整文档变更。
 */
class DocumentGeomUndo final : public UndoItem
{
public:
    /** 单层完整快照（属性 + 像素 + 层尺寸）。 */
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
    int m_width = 0;                       ///< 文档宽
    int m_height = 0;                      ///< 文档高
    int m_activeIndex = -1;                ///< 活动层索引
    QImage m_selectionMask;                ///< 选区灰度 mask
    std::vector<LayerState> m_layers;      ///< 各层快照（顺序与 LayerStack 一致）
    QString m_label;
};

} // namespace Ps

#endif // UNDOITEM_H
