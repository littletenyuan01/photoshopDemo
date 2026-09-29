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

/**
 * 图像文档：一张「可编辑图」的根对象（domain）。
 * 持有尺寸、图层栈、活动层、文档级选区；UI 只通过 AppSession 观察，不另存一份像素。
 * 参考 GIMP 中 Image 与 Layer 的边界（无 PDB、无 XCF）。
 *
 * 【信号分级】刻意拆成多条，让订阅方按需增量更新，避免「一改就整表重建」：
 *
 * | 信号                        | 触发场景                       | 谁该订阅                       |
 * |-----------------------------|--------------------------------|--------------------------------|
 * | pixelsChanged(rect)         | 画笔/橡皮/滤镜写像素 + 脏区     | 画布（将来只重合成 rect）        |
 * | layerPropertiesChanged(i)   | 显隐/不透明度/名称/混合模式      | 图层面板（只改第 i 行）          |
 * | structureChanged()          | 增删/排序（层数或下标变了）      | 图层面板（唯一需要重建列表的）    |
 * | activeLayerChanged(i)       | 当前编辑目标变了                | 画布/面板（只更选中态）          |
 * | selectionChanged()          | 选区 mask 变了（不含图层像素）   | 画布蚂蚁线（无需重合成）         |
 * | contentChanged()            | 像素/结构/属性/尺寸变化（不含纯选区） | 粗粒度订阅方                  |
 *
 * contentChanged 是**汇总信号**，恒在上述四条之后发射，便于状态栏等不需要区分细节的订阅方。
 *
 * 【脏区语义】markDirty(rect) 记录**累计脏区**（m_dirtyRect 并集），画布可据此只重合成局部。
 * 当前 CanvasView 仍是全量重合成，但接口已就位（见 docs/architecture.md §8.1 主线 ②）。
 */
class ImageDocument : public QObject
{
    Q_OBJECT

public:
    explicit ImageDocument(int width, int height, QObject *parent = nullptr);

    /** 工厂：白底（或指定色）单层背景文档。 */
    static std::unique_ptr<ImageDocument> createBlank(int width, int height,
                                                      const QColor &background = Qt::white);

    int width() const { return m_width; }
    int height() const { return m_height; }

    /** 只读遍历用；**改动图层请走本类的语义化 setter / addLayer / removeLayer**。 */
    LayerStack &layers() { return m_layers; }
    const LayerStack &layers() const { return m_layers; }

    int activeLayerIndex() const { return m_activeLayerIndex; }
    void setActiveLayerIndex(int index);

    Layer *activeLayer();
    const Layer *activeLayer() const;

    /** 文档级选区（对照 gimp_image_get_mask）；始终存在，空选区 = mask 全 0。 */
    Selection &selection() { return m_selection; }
    const Selection &selection() const { return m_selection; }

    /** 清空选区（对照 Select → None / Ctrl+D）。 */
    void clearSelection();
    /** 全选（对照 Select → All / Ctrl+A）。 */
    void selectAll();
    /** 反选（对照 Select → Invert）。 */
    void invertSelection();
    /**
     * 矩形写入选区（对照 gimp_channel_select_rectangle）。
     * @param rect 文档坐标；@param op 替换/加/减/交
     */
    void selectRectangle(const QRect &rect, ChannelOp op);
    /** 椭圆写入选区（对照 gimp_channel_select_ellipse）；内接于 rect。 */
    void selectEllipse(const QRect &rect, ChannelOp op);
    /**
     * 图层 alpha → 选区（对照 gimp_channel_select_alpha / PS Ctrl+点缩略图）。
     * @param layerIndex 栈下标；@param op 替换/加/减/交
     */
    void selectLayerAlpha(int layerIndex, ChannelOp op = ChannelOp::Replace);

    /**
     * 自顶向下点选图层（对照 gimp_image_pick_layer）。
     * @return 命中层下标；点在透明/空白处返回 -1
     */
    int pickLayerAt(int docX, int docY) const;

    bool isDirty() const { return m_dirty; }
    void clearDirty();

    /** 工程文件路径（空 = 尚未存储过）。 */
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    /**
     * 用灰度图替换选区 mask（工程加载）；发 selectionChanged。
     * 尺寸会适配到当前文档大小。
     */
    void replaceSelectionMask(const QImage &mask);

    // —— 脏区 ——

    /**
     * 标记脏区并广播（像素写入后由工具/滤镜调用）。
     * @param rect 图像坐标系脏矩形；空矩形会被忽略。
     */
    void markDirty(const QRect &rect);
    /** 整图脏（图层增删、尺寸变化、粗粒度改动走这里）。 */
    void markDirty();
    /** 自上次 clearDirtyRect 以来累计的脏区。 */
    const QRect &dirtyRect() const { return m_dirtyRect; }
    void clearDirtyRect() { m_dirtyRect = QRect(); }

    // —— 语义化 setter（UI 只该调这些，对应 GIMP actions 层的收口）——

    /** 改显隐；越界或值未变则忽略。 */
    void setLayerVisible(int index, bool visible);
    /** 改不透明度（[0,1]）；越界或值未变则忽略。 */
    void setLayerOpacity(int index, qreal opacity);
    /** 改图层名；越界或名未变则忽略。 */
    void setLayerName(int index, const QString &name);
    /** 改混合模式。 */
    void setLayerBlendMode(int index, BlendMode mode);

    /**
     * 平移图层（对照 gimp_item_translate / 移动工具）。
     * 只改 Layer offset，不搬瓦片像素；脏区 = 旧外接矩形 ∪ 新外接矩形。
     */
    void translateLayer(int index, int dx, int dy);

    // —— 结构操作 ——

    /**
     * **图层入栈的唯一入口**：在这里统一挂 `owner`（Layer 靠它广播属性信号）。
     * 早先允许调用方直接 `layers().addLayer()`，导致 createBlank 与「打开图片」
     * 两处漏挂 owner，改背景层显隐/透明度时信号不发、画布与面板静默不同步。
     * 现在 `LayerStack` 的改栈方法已设为 private，绕过本函数会**编译不过**。
     * @return 新层下标；layer 为空返回 -1。会发 structureChanged + contentChanged。
     */
    int addLayer(std::unique_ptr<Layer> layer);

    /** 在栈顶新增透明层，设为活动层。返回新层下标。 */
    int addTransparentLayer(const QString &name = QString());
    /**
     * 复制图层（对照 GIMP `layers-duplicate` / `gimp_item_duplicate` + `gimp_image_add_layer`）。
     * 深拷贝像素与属性，插入到源层上方（栈下标 +1），并设为活动层。
     * @return 新层下标；失败 -1
     */
    int duplicateLayer(int index);
    /** 删除指定层；至少保留一层。删除后修正活动层下标。 */
    bool removeLayer(int index);

    /**
     * 图像大小：重采样缩放所有图层到 newWidth×newHeight（对齐 PS「重新采样」开启）。
     * 尺寸未变则忽略。会发 structureChanged + contentChanged。
     */
    void scaleImage(int newWidth, int newHeight);

    /**
     * 画布大小：改文档工作台尺寸，图层内容按锚点平移（不缩放像素）。
     * @param anchorRow / anchorCol 0=上/左，1=中，2=下/右
     * @param extensionColor 扩大时底层空白区域填充色；透明则保持透明
     */
    void resizeCanvas(int newWidth, int newHeight,
                      int anchorRow, int anchorCol,
                      const QColor &extensionColor);

    /** 由 Layer::notifyPropertiesChanged 调用；UI 一般不直接调。 */
    void notifyLayerPropertiesChanged(const Layer &layer);

signals:
    /** 像素脏区变化（图像坐标）；画布将来可只重合成 rect。 */
    void pixelsChanged(const QRect &rect);
    /** 第 index 层属性变了；图层面板只刷该行。 */
    void layerPropertiesChanged(int index);
    /** 层数/顺序变了；图层面板需重建列表。 */
    void structureChanged();
    /** 活动层下标变了。 */
    void activeLayerChanged(int index);
    /** 选区 mask 变了（不触发 contentChanged，避免无谓重合成）。 */
    void selectionChanged();
    /** 汇总信号：像素/结构/属性/尺寸变化；纯选区改动不发。 */
    void contentChanged();

private:
    /** 按层指针反查下标；不属于本栈返回 -1。 */
    int indexOfLayer(const Layer *layer) const;

    int m_width = 0;   ///< 文档像素宽
    int m_height = 0;  ///< 文档像素高
    LayerStack m_layers;
    Selection m_selection;       ///< 文档级选区 mask（对照 GimpImage::selection_mask）
    int m_activeLayerIndex = -1; ///< -1 = 无活动层
    bool m_dirty = false;        ///< 相对「已保存」的脏标记
    QString m_filePath;          ///< 关联的 .pslite 路径；空表示未存储
    QRect m_dirtyRect;           ///< 累计像素脏区（图像坐标）
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
