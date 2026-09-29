#ifndef IMAGEDOCUMENT_H
#define IMAGEDOCUMENT_H

#include "blendmode.h"
#include "layerstack.h"
#include "selection.h"

#include <QColor>
#include <QObject>
#include <QRect>
#include <QString>
#include <memory>

namespace Ps {

class HistoryStack;
class Layer;
class LayerPixelsUndo;
class LayerPropUndo;
class LayerStructureUndo;
class DocumentGeomUndo;

/**
 * 图像文档：一张「可编辑图」的根对象（domain）。
 * 持有尺寸、图层栈、活动层、文档级选区；UI 只通过 AppSession 观察，不另存一份像素。
 *
 * 撤销：挂 HistoryStack（对照 GIMP GimpImage 上的 undo_stack）；
 * 改状态前 push，UndoItem::pop 对调。加载/createBlank 用 HistorySuppress 跳过记录。
 */
class ImageDocument : public QObject
{
    Q_OBJECT

public:
    explicit ImageDocument(int width, int height, QObject *parent = nullptr);
    ~ImageDocument() override;

    static std::unique_ptr<ImageDocument> createBlank(int width, int height,
                                                      const QColor &background = Qt::white);

    int width() const { return m_width; }
    int height() const { return m_height; }

    LayerStack &layers() { return m_layers; }
    const LayerStack &layers() const { return m_layers; }

    int activeLayerIndex() const { return m_activeLayerIndex; }
    void setActiveLayerIndex(int index);

    Layer *activeLayer();
    const Layer *activeLayer() const;

    Selection &selection() { return m_selection; }
    const Selection &selection() const { return m_selection; }

    void clearSelection();
    void selectAll();
    void invertSelection();
    void selectRectangle(const QRect &rect, ChannelOp op);
    void selectEllipse(const QRect &rect, ChannelOp op);
    void selectLayerAlpha(int layerIndex, ChannelOp op = ChannelOp::Replace);

    /**
     * 编辑→清除（Delete）：活动层可见像素擦为透明。
     * 有选区只清 mask 内；无选区清整层。对照 GIMP edit-clear。
     * @return false 若无活动层 / 层隐藏。
     */
    bool clearActiveLayerPixels();
    /**
     * 编辑→填充（Shift+F5）：用 @p color 填活动层（Demo 无对话框，默认前景色）。
     * 有选区只填 mask 内；无选区填整层。选区保留。
     */
    bool fillActiveLayer(const QColor &color);

    int pickLayerAt(int docX, int docY) const;

    bool isDirty() const { return m_dirty; }
    void clearDirty();

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    void replaceSelectionMask(const QImage &mask);

    void markDirty(const QRect &rect);
    void markDirty();
    const QRect &dirtyRect() const { return m_dirtyRect; }
    void clearDirtyRect() { m_dirtyRect = QRect(); }

    HistoryStack &history() { return *m_history; }
    const HistoryStack &history() const { return *m_history; }
    void undo();
    void redo();

    /** 绘制类改像素前：整层快照（对照 drawable push_undo；一笔一条）。 */
    void pushLayerPixelsUndo(int layerIndex, const QString &label);
    /** 移动拖拽开始前：offset 属性快照。 */
    void pushLayerOffsetUndo(int layerIndex);
    /** 图像/画布大小前：整文档几何快照。 */
    void pushDocumentGeomUndo(const QString &label);

    class HistorySuppress
    {
    public:
        explicit HistorySuppress(ImageDocument &doc);
        ~HistorySuppress();
        HistorySuppress(const HistorySuppress &) = delete;
        HistorySuppress &operator=(const HistorySuppress &) = delete;
    private:
        ImageDocument &m_doc;
    };

    void setLayerVisible(int index, bool visible);
    void setLayerOpacity(int index, qreal opacity);
    void setLayerName(int index, const QString &name);
    /**
     * 改混合模式。
     * @param recordHistory false 用于下拉悬停预览（begin 时已 push 一条）。
     */
    void setLayerBlendMode(int index, BlendMode mode, bool recordHistory = true);
    void translateLayer(int index, int dx, int dy);

    /** 属性类改动前手动 push（混合模式预览 begin 用）。 */
    void pushLayerPropertiesUndo(int index, const QString &label);

    int addLayer(std::unique_ptr<Layer> layer);
    int addTransparentLayer(const QString &name = QString());
    int duplicateLayer(int index);
    bool removeLayer(int index);

    void scaleImage(int newWidth, int newHeight);
    void resizeCanvas(int newWidth, int newHeight,
                      int anchorRow, int anchorCol,
                      const QColor &extensionColor);

    void notifyLayerPropertiesChanged(const Layer &layer);

signals:
    void pixelsChanged(const QRect &rect);
    void layerPropertiesChanged(int index);
    void structureChanged();
    void activeLayerChanged(int index);
    void selectionChanged();
    void contentChanged();

private:
    friend class HistorySuppress;
    friend class LayerPixelsUndo;
    friend class LayerPropUndo;
    friend class LayerStructureUndo;
    friend class DocumentGeomUndo;

    bool shouldRecordHistory() const;
    void pushLayerPropUndo(int index, const QString &label);
    int indexOfLayer(const Layer *layer) const;

    /** UndoItem::pop 专用（friend），可改 LayerStack。 */
    std::unique_ptr<Layer> takeLayerForUndo(int index);
    void insertLayerForUndo(int index, std::unique_ptr<Layer> layer);

    int m_width = 0;
    int m_height = 0;
    LayerStack m_layers;
    Selection m_selection;
    int m_activeLayerIndex = -1;
    bool m_dirty = false;
    QString m_filePath;
    QRect m_dirtyRect;
    std::unique_ptr<HistoryStack> m_history;
    int m_historySuppress = 0;
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
