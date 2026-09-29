/**
 * imagedocument.h — 图像文档根对象：尺寸、图层栈、活动层、选区与分级信号（domain 层）。
 *
 * UI 经 AppSession 订阅；改层属性走语义化 setter；像素脏区由 markDirty 累计。
 */
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

    /** 创建带单色背景层的空白文档（跳过撤销记录）。 */
    static std::unique_ptr<ImageDocument> createBlank(int width, int height,
                                                      const QColor &background = Qt::white);

    int width() const { return m_width; }
    int height() const { return m_height; }

    LayerStack &layers() { return m_layers; }
    const LayerStack &layers() const { return m_layers; }

    int activeLayerIndex() const { return m_activeLayerIndex; }
    /** 切换活动层；越界忽略。发 activeLayerChanged + contentChanged。 */
    void setActiveLayerIndex(int index);

    Layer *activeLayer();
    const Layer *activeLayer() const;

    Selection &selection() { return m_selection; }
    const Selection &selection() const { return m_selection; }

    /** 清空选区并发 selectionChanged。 */
    void clearSelection();
    /** 全选并发 selectionChanged。 */
    void selectAll();
    /** 反相选区并发 selectionChanged。 */
    void invertSelection();
    /** 矩形选区写入（ChannelOp 组合）并发 selectionChanged。 */
    void selectRectangle(const QRect &rect, ChannelOp op);
    /** 椭圆选区写入并发 selectionChanged。 */
    void selectEllipse(const QRect &rect, ChannelOp op);
    /** 由指定图层 alpha 建立选区；默认 Replace。 */
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

    /**
     * 给活动层追加亮度/对比度滤镜节点（非破坏，不写瓦片）。
     * @return 滤镜下标；失败 -1。
     */
    int addBrightnessContrastFilter(qreal brightness = 0.12, qreal contrast = 0.18);
    /** 开关指定滤镜；index 越界返回 false。 */
    bool setLayerFilterEnabled(int layerIndex, int filterIndex, bool enabled);
    /** 移除指定滤镜。 */
    bool removeLayerFilter(int layerIndex, int filterIndex);

    /**
     * 自顶向下点选图层（对照 PS 自动选择）。
     * @return 层下标；未命中 -1。
     */
    int pickLayerAt(int docX, int docY) const;

    /** 自上次保存以来是否有未保存变更。 */
    bool isDirty() const { return m_dirty; }
    /** 保存成功后清除 dirty 标志与脏区。 */
    void clearDirty();

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    /** 用灰度图整体替换选区 mask（工程加载用）。 */
    void replaceSelectionMask(const QImage &mask);

    /** 标记文档坐标脏区，累计 m_dirtyRect，发 pixelsChanged + contentChanged。 */
    void markDirty(const QRect &rect);
    /** 整文档 markDirty。 */
    void markDirty();
    /** 自上次 clearDirty 以来累计的脏区（文档坐标）。 */
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

    /** RAII：构造时递增抑制计数，析构恢复；加载/createBlank 期间跳过撤销记录。 */
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

    /** 语义化 setter：改可见性并 push 撤销（UI 入口，勿直接调 Layer::setVisible）。 */
    void setLayerVisible(int index, bool visible);
    /** 语义化 setter：改不透明度并 push 撤销。 */
    void setLayerOpacity(int index, qreal opacity);
    /** 语义化 setter：改图层名并 push 撤销。 */
    void setLayerName(int index, const QString &name);
    /**
     * 改混合模式。
     * @param recordHistory false 用于下拉悬停预览（begin 时已 push 一条）。
     */
    void setLayerBlendMode(int index, BlendMode mode, bool recordHistory = true);
    /** 平移图层 offset 并按新旧 bounds 并集 markDirty。 */
    void translateLayer(int index, int dx, int dy);

    /** 属性类改动前手动 push（混合模式预览 begin 用）。 */
    void pushLayerPropertiesUndo(int index, const QString &label);

    /**
     * 入栈唯一入口：挂 owner、可选结构撤销、发 structureChanged。
     * @return 新层下标；-1 失败。
     */
    int addLayer(std::unique_ptr<Layer> layer);
    /** 新建透明层并设为活动层。 */
    int addTransparentLayer(const QString &name = QString());
    /** 复制指定层并插入其上方，设为活动层。 */
    int duplicateLayer(int index);
    /** 删除层（至少保留一层）；发 structureChanged。 */
    bool removeLayer(int index);

    /** 图像大小：重采样各层像素与选区 mask。 */
    void scaleImage(int newWidth, int newHeight);
    /**
     * 画布大小：按锚点扩展/裁切，图层与选区随 offset 贴入新画布。
     * @param anchorRow/Col 0..2 九宫格锚点（0=上/左，1=中，2=下/右）
     */
    void resizeCanvas(int newWidth, int newHeight,
                      int anchorRow, int anchorCol,
                      const QColor &extensionColor);

    /** Layer 属性 setter 回调：标脏并发 layerPropertiesChanged + contentChanged。 */
    void notifyLayerPropertiesChanged(const Layer &layer);
    /** 仅名称等标签变化：不标投影脏区、不发 contentChanged。 */
    void notifyLayerLabelChanged(const Layer &layer);

signals:
    void pixelsChanged(const QRect &rect);       ///< 文档坐标脏区（投影增量更新用）
    void layerPropertiesChanged(int index);      ///< 单图层属性/标签变更
    void structureChanged();                     ///< 层数/顺序/文档尺寸变更
    void activeLayerChanged(int index);          ///< 活动层切换
    void selectionChanged();                     ///< 选区 mask 变更
    void contentChanged();                       ///< 汇总：任意需整 UI 刷新时

private:
    friend class HistorySuppress;
    friend class LayerPixelsUndo;
    friend class LayerPropUndo;
    friend class LayerStructureUndo;
    friend class DocumentGeomUndo;

    /** 是否应 push 撤销（未抑制且非 redo/undo 回放中）。 */
    bool shouldRecordHistory() const;
    void pushLayerPropUndo(int index, const QString &label);
    /** 由 Layer 指针反查栈下标；-1 未找到。 */
    int indexOfLayer(const Layer *layer) const;

    /** UndoItem::pop 专用（friend），可改 LayerStack。 */
    std::unique_ptr<Layer> takeLayerForUndo(int index);
    void insertLayerForUndo(int index, std::unique_ptr<Layer> layer);

    int m_width = 0;                             ///< 文档宽度（像素）
    int m_height = 0;                            ///< 文档高度（像素）
    LayerStack m_layers;                         ///< 自底向顶图层栈
    Selection m_selection;                       ///< 文档级选区 mask
    int m_activeLayerIndex = -1;                 ///< 活动层下标；-1 无
    bool m_dirty = false;                        ///< 未保存变更标志
    QString m_filePath;                          ///< 关联磁盘路径（可为空）
    QRect m_dirtyRect;                           ///< 累计脏区（文档坐标）
    std::unique_ptr<HistoryStack> m_history;     ///< 撤销栈
    int m_historySuppress = 0;                   ///< >0 时跳过 push 撤销
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
