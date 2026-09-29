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
    virtual QString name() const = 0;
    virtual quint64 byteSize() const = 0;
    virtual void pop(ImageDocument &doc) = 0;
};

struct LayerPropSnapshot {
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    int offsetX = 0;
    int offsetY = 0;
};

LayerPropSnapshot captureLayerProps(const Layer &layer);
void applyLayerProps(Layer &layer, const LayerPropSnapshot &s);

class LayerPixelsUndo final : public UndoItem
{
public:
    LayerPixelsUndo(int layerIndex, QImage pixels, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override;
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1;
    QImage m_pixels;
    QString m_label;
};

class LayerPropUndo final : public UndoItem
{
public:
    LayerPropUndo(int layerIndex, LayerPropSnapshot before, const QString &label);
    QString name() const override { return m_label; }
    quint64 byteSize() const override { return sizeof(*this) + quint64(m_snap.name.size()) * 2; }
    void pop(ImageDocument &doc) override;

private:
    int m_layerIndex = -1;
    LayerPropSnapshot m_snap;
    QString m_label;
};

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
