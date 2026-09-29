#ifndef ENGINE_PREMUL_H
#define ENGINE_PREMUL_H

#include <QColor>
#include <QRgb>
#include <QtGlobal>

namespace Ps {
namespace Premul {

/** 直通 QColor → 预乘 ARGB32。 */
inline QRgb toPremultipliedRgb(const QColor &color)
{
    const int a = qBound(0, color.alpha(), 255);
    const int r = (color.red() * a + 127) / 255;
    const int g = (color.green() * a + 127) / 255;
    const int b = (color.blue() * a + 127) / 255;
    return qRgba(r, g, b, a);
}

/** 预乘像素 → 直通 RGB + alpha（0..255）。 */
inline void unpremultiplyRgb(QRgb px, int *r, int *g, int *b, int *a)
{
    *a = qAlpha(px);
    if (*a <= 0) {
        *r = *g = *b = 0;
        return;
    }
    if (*a >= 255) {
        *r = qRed(px);
        *g = qGreen(px);
        *b = qBlue(px);
        return;
    }
    *r = qBound(0, (qRed(px) * 255 + *a / 2) / *a, 255);
    *g = qBound(0, (qGreen(px) * 255 + *a / 2) / *a, 255);
    *b = qBound(0, (qBlue(px) * 255 + *a / 2) / *a, 255);
}

} // namespace Premul
} // namespace Ps

#endif // ENGINE_PREMUL_H
