#ifndef LAYER_H
#define LAYER_H

#include "blendmode.h"
#include "tilebuffer.h"

#include <QColor>
#include <QImage>
#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 单图层（domain 层）。
 * 像素真相在 TileBuffer（64×64 懒分配）中；格式 ARGB32 预乘。
 * 对应 GIMP GimpLayer + GeglBuffer 瓦片语义的瘦身版：无组层、无滤镜栈、无 scratch。
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
     */
    Layer(const QString &name, int width, int height);
    /** 用已有图像构造；按块拆入 TileBuffer。 */
    Layer(const QString &name, const QImage &pixels);

    QString name() const { return m_name; }
    void setName(const QString &name);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    BlendMode blendMode() const { return m_blendMode; }
    void setBlendMode(BlendMode mode);

    TileBuffer &tiles() { return m_tiles; }
    const TileBuffer &tiles() const { return m_tiles; }

    /** 是否已有任意已分配瓦片（透明新建层为 false）。 */
    bool hasPixelData() const { return !m_tiles.isEmpty(); }

    /** 拼成整层临时图（缩略图）；无瓦片时为全透明同尺寸图。 */
    QImage materialize() const { return m_tiles.materialize(); }

    int width() const { return m_tiles.width(); }
    int height() const { return m_tiles.height(); }

    /** 整层填充；全透明会释放全部瓦片。 */
    void fill(const QColor &color);

    void setOwner(ImageDocument *owner) { m_owner = owner; }

private:
    void notifyPropertiesChanged();

    ImageDocument *m_owner = nullptr;

    QString m_name;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    BlendMode m_blendMode = BlendMode::Normal;
    TileBuffer m_tiles;
};

} // namespace Ps

#endif // LAYER_H
