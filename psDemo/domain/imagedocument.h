#ifndef IMAGEDOCUMENT_H
#define IMAGEDOCUMENT_H

#include "layerstack.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <memory>

namespace Ps {

/**
 * 图像文档：一张「可编辑图」的根对象（domain）。
 * 持有尺寸、图层栈、活动层；UI 只通过指针观察，不另存一份像素。
 * 参考 GIMP 中 Image 与 Layer 的边界（无 PDB、无 XCF）。
 *
 * 信号约定：
 * - documentChanged：像素或需重绘的内容变了
 * - structureChanged：层数/顺序等结构变了
 * - activeLayerChanged：当前编辑目标变了
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

    LayerStack &layers() { return m_layers; }
    const LayerStack &layers() const { return m_layers; }

    int activeLayerIndex() const { return m_activeLayerIndex; }
    void setActiveLayerIndex(int index);

    Layer *activeLayer();
    const Layer *activeLayer() const;

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    bool isDirty() const { return m_dirty; }
    /** 标记已修改并发 documentChanged（画布应重合成）。 */
    void markDirty();
    void clearDirty();

    /** 在栈顶新增透明层，设为活动层，并发 structureChanged + documentChanged。 */
    int addTransparentLayer(const QString &name = QString());
    /**
     * 删除指定层；至少保留一层（保证合成仍有内容）。
     * 会修正 activeLayerIndex，并通知结构/活动层/重绘。
     */
    bool removeLayer(int index);
    /** 显隐/透明度等「只影响合成观感」的变更 → documentChanged。 */
    void notifyLayerVisualChanged();
    /** 增删/排序后由外部调用，或内部增删已自动发出。 */
    void notifyStructureChanged();

signals:
    void documentChanged();
    void activeLayerChanged(int index);
    void structureChanged();

private:
    int m_width = 0;
    int m_height = 0;
    LayerStack m_layers;
    int m_activeLayerIndex = -1; // -1 表示无活动层
    QString m_filePath;
    bool m_dirty = false;
};

} // namespace Ps

#endif // IMAGEDOCUMENT_H
