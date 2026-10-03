/**
 * layermask.cpp — LayerMask 实现。
 */
#include "layermask.h"

#include <cstring>

namespace Ps {

LayerMask::LayerMask(int width, int height, quint8 fill)
{
    if (width <= 0 || height <= 0)
        return;
    m_gray = QImage(width, height, QImage::Format_Grayscale8);
    m_gray.fill(fill);
}

quint8 LayerMask::valueAt(int lx, int ly) const
{
    if (m_gray.isNull() || lx < 0 || ly < 0 || lx >= m_gray.width() || ly >= m_gray.height())
        return 0;
    return m_gray.constScanLine(ly)[lx];
}

void LayerMask::fill(quint8 v)
{
    if (!m_gray.isNull())
        m_gray.fill(v);
}

void LayerMask::expand(int padL, int padT, int padR, int padB, quint8 fill)
{
    if (m_gray.isNull() || (padL == 0 && padT == 0 && padR == 0 && padB == 0))
        return;
    const int w = m_gray.width();
    const int h = m_gray.height();
    QImage neu(w + padL + padR, h + padT + padB, QImage::Format_Grayscale8);
    neu.fill(fill);
    for (int y = 0; y < h; ++y) {
        const uchar *src = m_gray.constScanLine(y);
        uchar *dst = neu.scanLine(y + padT) + padL;
        std::memcpy(dst, src, size_t(w));
    }
    m_gray = std::move(neu);
}

void LayerMask::shift(int dx, int dy, quint8 fill)
{
    if (m_gray.isNull() || (dx == 0 && dy == 0))
        return;
    const int w = m_gray.width();
    const int h = m_gray.height();
    QImage neu(w, h, QImage::Format_Grayscale8);
    neu.fill(fill);
    for (int y = 0; y < h; ++y) {
        const int sy = y - dy;
        if (sy < 0 || sy >= h)
            continue;
        const uchar *src = m_gray.constScanLine(sy);
        uchar *dst = neu.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int sx = x - dx;
            if (sx < 0 || sx >= w)
                continue;
            dst[x] = src[sx];
        }
    }
    m_gray = std::move(neu);
}

void LayerMask::setFromImage(const QImage &gray)
{
    if (gray.isNull()) {
        m_gray = QImage();
        return;
    }
    if (gray.format() == QImage::Format_Grayscale8)
        m_gray = gray.copy();
    else
        m_gray = gray.convertToFormat(QImage::Format_Grayscale8);
}

LayerMask LayerMask::clone() const
{
    LayerMask m;
    m.m_enabled = m_enabled;
    m.m_linked = m_linked;
    m.m_gray = m_gray.copy();
    return m;
}

} // namespace Ps
