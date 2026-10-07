/**
 * imagedocument.h — 图像文档根对象：尺寸、图层栈、活动层、选区与分级信号（domain 层）。
 *
 * UI 经 AppSession 订阅；改层属性走语义化 setter；像素脏区由 markDirty 累计。
 */
#ifndef IMAGEDOCUMENT_H
#define IMAGEDOCUMENT_H

#include "blendmode.h"
#include "filternode.h"
#include "layerstack.h"
#include "layerstyle.h"
#include "selection.h"
#include "engine/op/opname.h"

#include <QColor>
#include <QObject>
#include <QPoint>
#include <QPolygonF>
#include <QRect>
#include <QString>
#include <QVector>
#include <memory>

namespace Ps {

class HistoryStack;
class Layer;
class LayerPixelsUndo;
class LayerPropUndo;
class LayerStructureUndo;
class LayerMoveUndo;
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
    /**
     * 多边形选区写入（自由套索等）；经 PaintEngine → SelectPolygonOp。
     * 对照 gimp_channel_select_polygon。
     */
    void selectPolygon(const QPolygonF &points, ChannelOp op);
    /**
     * 连通域/相似色选区（魔棒）；经 PaintEngine → SelectFloodOp。
     * @param sampleMerged true=合成图取样（对照 sample-merged）；false=活动层
     */
    void selectFlood(const QPoint &seedDoc, int tolerance, bool contiguous,
                     bool sampleMerged, ChannelOp op);
    /** 由指定图层 alpha 建立选区；默认 Replace。 */
    void selectLayerAlpha(int layerIndex, ChannelOp op = ChannelOp::Replace);
    /** 由指定图层蒙版灰度建立选区；无蒙版则忽略。 */
    void selectLayerMask(int layerIndex, ChannelOp op = ChannelOp::Replace);

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
    /**
     * 新建调整图层（对照 PS：独立层种 + 默认白色蒙版 + 属性面板调参）。
     * GIMP 原生无此层种，等价物是 drawable filter；本 Demo 按 PS 语义。
     * @return 新层下标；失败 -1。
     */
    int addAdjustmentLayer(OpName op);
    /** @deprecated 请用 addAdjustmentLayer(OpName::BrightnessContrast)。 */
    int addBrightnessContrastAdjustmentLayer(qreal brightness = 0.12,
                                             qreal contrast = 0.18);
    /**
     * 替换指定滤镜节点参数（调整层属性面板）。
     * @param pushUndo 滑条松手时 true；拖动中 false 仅预览。
     * 预览：只改节点并发 adjustmentPreviewChanged（画布 live，不整幅 markDirty）。
     * 提交：pushUndo + adjustmentPreviewCommit；画布 adopt live 定稿。
     */
    bool setLayerFilterNode(int layerIndex, int filterIndex, const FilterNode &node,
                            bool pushUndo);
    /** 开关指定滤镜；index 越界返回 false。 */
    bool setLayerFilterEnabled(int layerIndex, int filterIndex, bool enabled);
    /** 移除指定滤镜。 */
    bool removeLayerFilter(int layerIndex, int filterIndex);

    /**
     * 给活动层确保一条图层样式（无则追加默认并启用；已有则打开）。
     * 对照 GIMP append_new_filter / PS 图层样式。
     * @return 效果下标；失败 -1。
     */
    int ensureActiveLayerStyle(LayerStyleKind kind);
    /** 用整栈替换活动层样式（对话框确认）。 */
    bool replaceActiveLayerStyles(const QVector<LayerStyleEffect> &effects);
    /** 清除活动层全部样式。 */
    bool clearActiveLayerStyles();
    /**
     * 开关指定层的某条样式（图层面板效果子行眼睛）。
     * @return false 若层或样式下标无效。
     */
    bool setLayerStyleEnabled(int layerIndex, int styleIndex, bool enabled);

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
    /** 选区变更前：mask 快照。 */
    void pushSelectionUndo(const QString &label);
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
    /**
     * 平移图层 offset。
     * @param emitContent false 时只记脏区、不发 contentChanged（移动工具 live 预览用）。
     */
    void translateLayer(int index, int dx, int dy, bool emitContent = true);
    /**
     * 仅平移蒙版灰度（取消链接后单独移动蒙版）；层 offset 不变。
     * 冻结预览时只累计脏区。
     */
    void shiftLayerMask(int index, int dx, int dy);

    /**
     * 图层蒙版初始化方式（对照 PS 图层→图层蒙版 / GIMP layers-add-mask）。
     * Reveal*：选区(或全层)为白=显示；Hide*：选区(或全层)为黑=隐藏。
     */
    enum class LayerMaskInit {
        RevealAll = 0,        ///< 全白
        HideAll = 1,          ///< 全黑
        RevealSelection = 2,  ///< 选区白、外黑；无选区≈RevealAll
        HideSelection = 3,    ///< 选区黑、外白；无选区≈HideAll
    };

    /**
     * 给指定层添加/替换图层蒙版并 push 撤销。
     * @return false 若层无效。
     */
    /**
     * 为图层添加蒙版。成功后：活动层自动进入蒙版编辑；若当时有选区则取消选区
     * （避免选区继续裁剪蒙版绘制，导致黑区涂不上）。
     */
    bool addLayerMask(int index, LayerMaskInit init = LayerMaskInit::RevealAll);
    /** 删除指定层蒙版；无蒙版则 no-op。 */
    bool removeLayerMask(int index);
    /**
     * 应用蒙版：把灰度乘进层像素 alpha 后删除蒙版（破坏性）。
     * 对照 PS「应用图层蒙版」；一条撤销同时恢复像素与蒙版。
     */
    bool applyLayerMask(int index);
    /** 启用/禁用蒙版（仍保留灰度数据）；无蒙版返回 false。 */
    bool setLayerMaskEnabled(int index, bool enabled);
    /** 链接/取消链接蒙版与图层；无蒙版返回 false。 */
    bool setLayerMaskLinked(int index, bool linked);

    /**
     * 是否正在编辑活动层的图层蒙版（对照 PS 点蒙版缩略图进入蒙版绘制）。
     * 画笔/橡皮写灰度；点图层缩略图切回像素。
     */
    bool isEditingLayerMask() const { return m_editingLayerMask; }
    /**
     * 切换蒙版编辑目标。@p on 且活动层无蒙版时返回 false。
     * 发 editingTargetChanged + layerPropertiesChanged（刷新行高亮）。
     */
    bool setEditingLayerMask(bool on);

    /**
     * 预览冻结（对照 GIMP `gimp_viewable_preview_freeze`）：
     * 只抑制图层面板缩略图刷新；**不**阻止投影合成（拖层仍走脏区 + idle）。
     * 解冻时补发一次缩略图相关信号。
     */
    void beginPreviewFreeze();
    void endPreviewFreeze();
    bool isPreviewFrozen() const { return m_previewFrozen; }

    /** 属性类改动前手动 push（混合模式预览 begin 用）。 */
    void pushLayerPropertiesUndo(int index, const QString &label);

    /**
     * 入栈唯一入口：挂 owner、可选结构撤销、发 structureChanged。
     * @param undoLabel 空则用「新建图层」
     * @return 新层下标；-1 失败。
     */
    int addLayer(std::unique_ptr<Layer> layer, const QString &undoLabel = QString());
    /** 新建透明层并设为活动层。 */
    int addTransparentLayer(const QString &name = QString());
    /**
     * 把位图置入为新图层（对照 GIMP file_open_layers + gimp_image_add_layers）。
     * 文档尺寸不变；图层按文档中心对齐 offset；设为活动层。
     * @param name 层名（建议用文件名）；空则自动编号
     * @return 新层下标；-1 失败（空图 / 无文档尺寸）
     */
    int placeImageAsLayer(const QImage &image, const QString &name = QString());
    /**
     * 置入链接图层（对照 GIMP file_open_layers(..., as_link) + GimpLinkLayer）。
     * 读 @p absolutePath 为缓存像素，层记录路径；文档尺寸不变、居中。
     * @return 新层下标；-1 失败
     */
    int placeLinkedImageAsLayer(const QString &absolutePath, const QString &name = QString());
    /**
     * 从链接路径重读像素（对照 gimp_link_layer 刷新 buffer）。
     * 保持当前 offset；尺寸变化时左上角不动。
     */
    bool updateLinkedLayer(int index);
    /**
     * 栅格化链接层：清除路径，保留当前像素（对照 GimpRasterizable）。
     * 之后可正常绘制。
     */
    bool rasterizeLinkedLayer(int index);
    /** 复制指定层并插入其上方，设为活动层。 */
    int duplicateLayer(int index);
    /** 删除层（至少保留一层）；发 structureChanged。 */
    bool removeLayer(int index);
    /**
     * 重排图层（对照 GIMP gimp_image_reorder_item）。
     * @param from 原栈下标；@param to 移动后的最终下标（0=底 … count-1=顶）
     * @return 是否发生了移动
     */
    bool moveLayer(int from, int to);

    /** 图像大小：重采样各层像素与选区 mask。 */
    void scaleImage(int newWidth, int newHeight);
    /**
     * 画布大小：按锚点扩展/裁切，图层与选区随 offset 贴入新画布。
     * @param anchorRow/Col 0..2 九宫格锚点（0=上/左，1=中，2=下/右）
     */
    void resizeCanvas(int newWidth, int newHeight,
                      int anchorRow, int anchorCol,
                      const QColor &extensionColor);

    /**
     * 裁剪文档到 @p rect（文档坐标，与画布求交）。
     * 对照 gimp_image_crop：各层贴到文档坐标后裁切，选区同步平移。
     */
    void cropTo(const QRect &rect);

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
    void editingTargetChanged();                 ///< 像素 ↔ 蒙版编辑目标切换
    void contentChanged();                       ///< 汇总：任意需整 UI 刷新时
    /**
     * 调整层参数拖动预览（对照 GIMP 滤镜对话框 ROI 预览 / PS 属性滑条）。
     * 节点已写入层；画布应只重跑滤镜叠到缓存的 below/above，勿全栈 Projection::sync。
     */
    void adjustmentPreviewChanged(int layerIndex);
    /** 调整层参数提交：画布应 adopt live 定稿（或回退 markDirty+sync）。 */
    void adjustmentPreviewCommit(int layerIndex);

private:
    friend class HistorySuppress;
    friend class LayerPixelsUndo;
    friend class LayerPropUndo;
    friend class LayerStructureUndo;
    friend class LayerMoveUndo;
    friend class DocumentGeomUndo;
    friend class SelectionUndo;

    /** 是否应 push 撤销（未抑制且非 redo/undo 回放中）。 */
    bool shouldRecordHistory() const;
    void pushLayerPropUndo(int index, const QString &label);
    /** 由 Layer 指针反查栈下标；-1 未找到。 */
    int indexOfLayer(const Layer *layer) const;

    /** UndoItem::pop 专用（friend），可改 LayerStack。 */
    std::unique_ptr<Layer> takeLayerForUndo(int index);
    void insertLayerForUndo(int index, std::unique_ptr<Layer> layer);
    /** 执行重排并广播（不 push 历史；供 moveLayer / LayerMoveUndo 共用）。 */
    void applyMoveLayer(int from, int to);

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
    bool m_previewFrozen = false;                ///< 拖层中冻结面板广播
    bool m_editingLayerMask = false;             ///< 活动层是否在编辑蒙版
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
