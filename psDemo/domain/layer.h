#ifndef LAYER_H
#define LAYER_H

#include "blendmode.h"

#include <QImage>
#include <QColor>
#include <QString>

namespace Ps {

/**
 * 单图层（domain 层）。
 * 像素真相存在 QImage 中，格式统一为 ARGB32 预乘，便于合成与后续 OpenCV 互转。
 * 对应 GIMP GimpLayer 的瘦身版：无组层、无滤镜栈。
 */
class Layer
{
public:
    /** 创建空白透明层。 */
    Layer(const QString &name, int width, int height);
    /** 用已有图像构造；内部会转成预乘格式。 */
    Layer(const QString &name, const QImage &pixels);

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    /** 不透明度 [0,1]，合成时乘到该层 alpha 上。 */
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    BlendMode blendMode() const { return m_blendMode; }
    void setBlendMode(BlendMode mode) { m_blendMode = mode; }

    /** 可写像素缓冲；画笔等应只改活动层的 pixels。 */
    QImage &pixels() { return m_pixels; }
    const QImage &pixels() const { return m_pixels; }

    int width() const { return m_pixels.width(); }
    int height() const { return m_pixels.height(); }

    /** 整层填充（用于背景层）；Qt 会按当前格式处理预乘。 */
    void fill(const QColor &color);

private:
    QString m_name;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    BlendMode m_blendMode = BlendMode::Normal;
    QImage m_pixels; // Format_ARGB32_Premultiplied
};

} // namespace Ps

#endif // LAYER_H
