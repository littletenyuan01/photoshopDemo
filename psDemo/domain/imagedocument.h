#ifndef IMAGEDOCUMENT_H
#define IMAGEDOCUMENT_H

#include "blendmode.h"
#include "layerstack.h"

#include <QColor>
#include <QObject>
#include <QRect>
#include <QString>
#include <memory>

namespace Ps {

/**
 * 图像文档：一张「可编辑图」的根对象（domain）。
 * 持有尺寸、图层栈、活动层；UI 只通过 AppSession 观察，不另存一份像素。
 * 参考 GIMP 中 Image 与 Layer 的边界（无 PDB、无 XCF）。
 *
 * 【信号分级】刻意拆成四条，让订阅方按需增量更新，避免「一改就整表重建」：
 *
 * | 信号                        | 触发场景                       | 谁该订阅                       |
 * |-----------------------------|--------------------------------|--------------------------------|
 * | pixelsChanged(rect)         | 画笔/橡皮/滤镜写像素 + 脏区     | 画布（将来只重合成 rect）        |
 * | layerPropertiesChanged(i)   | 显隐/不透明度/名称/混合模式      | 图层面板（只改第 i 行）          |
 * | structureChanged()          | 增删/排序（层数或下标变了）      | 图层面板（唯一需要重建列表的）    |
 * | activeLayerChanged(i)       | 当前编辑目标变了                | 画布/面板（只更选中态）          |
 * | contentChanged()            | 以上任意一种（含整图尺寸变化）   | 只关心「该重画了」的粗粒度订阅方  |
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

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    bool isDirty() const { return m_dirty; }
    void clearDirty();

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
    /** 删除指定层；至少保留一层。删除后修正活动层下标。 */
    bool removeLayer(int index);

    /** 由 Layer::notifyPropertiesChanged 调用；UI 一般不直接调。 */
    void notifyLayerPropertiesChanged(const Layer &layer);

signals:
    void pixelsChanged(const QRect &rect);
    void layerPropertiesChanged(int index);
    void structureChanged();
    void activeLayerChanged(int index);
    /** 汇总信号：以上任意一种都发；恒在其后发射。 */
    void contentChanged();

private:
    /** 按层指针反查下标；不属于本栈返回 -1。 */
    int indexOfLayer(const Layer *layer) const;

    int m_width = 0;
    int m_height = 0;
    LayerStack m_layers;
    int m_activeLayerIndex = -1; // -1 表示无活动层
    QString m_filePath;
    bool m_dirty = false;
    QRect m_dirtyRect; ///< 累计脏区（图像坐标）
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
