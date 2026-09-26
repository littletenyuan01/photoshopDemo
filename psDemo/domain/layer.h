#ifndef LAYER_H
#define LAYER_H

#include "blendmode.h"

#include <QImage>
#include <QColor>
#include <QString>

namespace Ps {

class ImageDocument;

/**
 * 单图层（domain 层）。
 * 像素真相存在 QImage 中，格式统一为 ARGB32 预乘，便于合成与后续 OpenCV 互转。
 * 对应 GIMP GimpLayer 的瘦身版：无组层、无滤镜栈。
 *
 * 【变更通知】本类持有 owner 回指（由 ImageDocument 在入栈时设置）。
 * 属性 setter 内部改值后会通知 owner 发信号，因此 **UI 调用 setter 即自动刷新**，
 * 无需再手动调用 notifyXxx()。这对应 GIMP 中「改 GimpLayer 属性 → GimpImage 发信号 →
 * 各 view 增量更新」的链路（app/core/gimplayer.c 的 notify 虚函数）。
 *
 * 【像素写入】pixels() 返回可写引用，写入方（工具/滤镜）**必须**自行调用
 * ImageDocument::markDirty(rect) 告知脏区，否则画布不会刷新。
 */
class Layer
{
public:
    /** 创建空白透明层。 */
    Layer(const QString &name, int width, int height);
    /** 用已有图像构造；内部会转成预乘格式。 */
    Layer(const QString &name, const QImage &pixels);

    QString name() const { return m_name; }
    /** 改名并按名变更发 layerPropertiesChanged。 */
    void setName(const QString &name);

    bool isVisible() const { return m_visible; }
    /** 改显隐并发 layerPropertiesChanged。 */
    void setVisible(bool visible);

    /** 不透明度 [0,1]，合成时乘到该层 alpha 上。 */
    qreal opacity() const { return m_opacity; }
    /** 改不透明度并发 layerPropertiesChanged（值被夹紧到 [0,1]）。 */
    void setOpacity(qreal opacity);

    BlendMode blendMode() const { return m_blendMode; }
    /** 改混合模式并发 layerPropertiesChanged。 */
    void setBlendMode(BlendMode mode);

    /**
     * 可写像素缓冲；画笔等应只改活动层的 pixels。
     * 【约定】写入后必须调 ImageDocument::markDirty(rect)。
     */
    QImage &pixels() { return m_pixels; }
    const QImage &pixels() const { return m_pixels; }

    int width() const { return m_pixels.width(); }
    int height() const { return m_pixels.height(); }

    /** 整层填充（用于背景层）；Qt 会按当前格式处理预乘。 */
    void fill(const QColor &color);

    /** 由 ImageDocument 在入栈时调用；不取得所有权。 */
    void setOwner(ImageDocument *owner) { m_owner = owner; }

private:
    /** 通知 owner：本层属性变了（UI 应只更新对应那一行，而非整表重建）。 */
    void notifyPropertiesChanged();

    ImageDocument *m_owner = nullptr; ///< 不拥有；图层栈由 ImageDocument 持有

    QString m_name;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    BlendMode m_blendMode = BlendMode::Normal;
    QImage m_pixels; // Format_ARGB32_Premultiplied
};

} // namespace Ps

#endif // LAYER_H
