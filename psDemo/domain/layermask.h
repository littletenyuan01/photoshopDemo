/**
 * layermask.h — 图层蒙版：层局部灰度图（domain 层）。
 *
 * 对照 GIMP GimpLayerMask / PS 图层蒙版：白=显示，黑=隐藏；合成时乘到层 alpha。
 * 本项目不引入 GEGL：数据挂在 Layer 上，由 Compositor 乘算（学 GIMP Applicator aux 逻辑）。
 */
#ifndef DOMAIN_LAYERMASK_H
#define DOMAIN_LAYERMASK_H

#include <QImage>
#include <QtGlobal>

namespace Ps {

class LayerMask
{
public:
    LayerMask() = default;
    /** 创建指定尺寸蒙版；@p fill 默认 255（全显示）。 */
    LayerMask(int width, int height, quint8 fill = 255);

    bool isNull() const { return m_gray.isNull(); }
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    /**
     * 是否与图层链接（对照 PS 蒙版链接图标）。
     * 链接时平移图层蒙版一起走；取消链接后平移层时蒙版留在文档位置。
     */
    bool isLinked() const { return m_linked; }
    void setLinked(bool on) { m_linked = on; }

    int width() const { return m_gray.width(); }
    int height() const { return m_gray.height(); }

    const QImage &image() const { return m_gray; }
    QImage &image() { return m_gray; }

    /** 层内坐标取样；越界视为 0（全藏）。 */
    quint8 valueAt(int lx, int ly) const;

    /** 重置为纯色（尺寸不变）；空图则忽略。 */
    void fill(quint8 v);

    /**
     * 按与 Layer::expandToIncludeLocal 相同的 pad 扩展；
     * 新区域填 @p fill（默认白=显示）。
     */
    void expand(int padL, int padT, int padR, int padB, quint8 fill = 255);

    /**
     * 平移灰度内容（层内）；新露出区域填 @p fill（默认白）。
     * 用于取消链接后「层动蒙版不动 / 只动蒙版」。
     */
    void shift(int dx, int dy, quint8 fill = 255);

    /** 换成新尺寸图（工程加载 / undo 恢复）。 */
    void setFromImage(const QImage &gray);

    /** 深拷贝快照。 */
    LayerMask clone() const;

private:
    QImage m_gray; ///< Format_Grayscale8，层局部坐标
    bool m_enabled = true;
    bool m_linked = true;
};

} // namespace Ps

#endif // DOMAIN_LAYERMASK_H
