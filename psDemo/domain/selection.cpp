/**
 * selection.cpp — Selection mask 重建、几何变更与形状合并（domain 层）。
 */
#include "selection.h"

#include <QPainter>
#include <QtGlobal>

#include <cstring>

namespace Ps {

/** 按文档尺寸创建全 0 mask。 */
Selection::Selection(int width, int height)
{
    reset(width, height);
}

void Selection::reset(int width, int height)
{
    width = qMax(0, width);
    height = qMax(0, height);
    if (width == 0 || height == 0) {
        m_mask = QImage();
        invalidateCache();
        return;
    }
    m_mask = QImage(width, height, QImage::Format_Grayscale8);
    m_mask.fill(0);
    invalidateCache();
}

void Selection::resizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY)
{
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    QImage neu(newWidth, newHeight, QImage::Format_Grayscale8);
    neu.fill(0);
    if (!m_mask.isNull()) {
        QPainter painter(&neu);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(offsetX, offsetY, m_mask);
        painter.end();
    }
    m_mask = neu;
    invalidateCache();
}

void Selection::scale(int newWidth, int newHeight)
{
    newWidth = qMax(1, newWidth);
    newHeight = qMax(1, newHeight);
    if (m_mask.isNull()) {
        reset(newWidth, newHeight);
        return;
    }
    // 选区 mask 用最近邻，避免羽化半透明边界被平滑糊开（简化；GIMP 有单独策略）
    m_mask = m_mask.scaled(newWidth, newHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (m_mask.format() != QImage::Format_Grayscale8)
        m_mask = m_mask.convertToFormat(QImage::Format_Grayscale8);
    invalidateCache();
}

void Selection::replaceFromImage(const QImage &mask)
{
    if (m_mask.isNull() || mask.isNull()) {
        clear();
        return;
    }
    QImage src = mask;
    if (src.format() != QImage::Format_Grayscale8)
        src = src.convertToFormat(QImage::Format_Grayscale8);
    if (src.size() != m_mask.size())
        src = src.scaled(m_mask.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    m_mask = src;
    invalidateCache();
}

void Selection::invalidateCache() const
{
    m_cacheValid = false;
}

void Selection::recomputeCache() const
{
    m_empty = true;
    m_bounds = QRect();
    if (m_mask.isNull()) {
        m_cacheValid = true;
        return;
    }

    const int w = m_mask.width();
    const int h = m_mask.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;

    for (int y = 0; y < h; ++y) {
        const uchar *line = m_mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (line[x] == 0)
                continue;
            m_empty = false;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    if (!m_empty)
        m_bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    m_cacheValid = true;
}

bool Selection::isEmpty() const
{
    if (!m_cacheValid)
        recomputeCache();
    return m_empty;
}

QRect Selection::bounds() const
{
    if (!m_cacheValid)
        recomputeCache();
    return m_bounds;
}

quint8 Selection::value(int docX, int docY) const
{
    if (m_mask.isNull() || docX < 0 || docY < 0
        || docX >= m_mask.width() || docY >= m_mask.height()) {
        return 0;
    }
    return m_mask.constScanLine(docY)[docX];
}

void Selection::fillRect(const QRect &rect, quint8 value)
{
    const QRect r = rect.intersected(m_mask.rect());
    if (r.isEmpty())
        return;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        uchar *line = m_mask.scanLine(y);
        memset(line + r.left(), value, size_t(r.width()));
    }
}

void Selection::clear()
{
    if (m_mask.isNull())
        return;
    m_mask.fill(0);
    invalidateCache();
}

void Selection::selectAll()
{
    if (m_mask.isNull())
        return;
    m_mask.fill(255);
    invalidateCache();
}

void Selection::invert()
{
    if (m_mask.isNull())
        return;
    const int w = m_mask.width();
    const int h = m_mask.height();
    for (int y = 0; y < h; ++y) {
        uchar *line = m_mask.scanLine(y);
        for (int x = 0; x < w; ++x)
            line[x] = static_cast<uchar>(255 - line[x]);
    }
    invalidateCache();
}

/** 矩形硬边写入；Replace 时空矩形等价 clear。 */
void Selection::selectRectangle(const QRect &rect, ChannelOp op)
{
    if (m_mask.isNull())
        return;

    const QRect r = rect.normalized().intersected(m_mask.rect());

    // 对照 gimp_channel_combine_rect：Replace 时空矩形 = 清空选区
    if (r.isEmpty()) {
        if (op == ChannelOp::Replace)
            clear();
        return;
    }

    switch (op) {
    case ChannelOp::Replace:
        m_mask.fill(0);
        fillRect(r, 255);
        break;
    case ChannelOp::Add:
        fillRect(r, 255);
        break;
    case ChannelOp::Subtract:
        fillRect(r, 0);
        break;
    case ChannelOp::Intersect: {
        // 矩形外清零，矩形内保留原值（已是 0/255 时等价于「只留相交部分」）
        const int w = m_mask.width();
        const int h = m_mask.height();
        for (int y = 0; y < h; ++y) {
            uchar *line = m_mask.scanLine(y);
            if (y < r.top() || y > r.bottom()) {
                memset(line, 0, size_t(w));
                continue;
            }
            if (r.left() > 0)
                memset(line, 0, size_t(r.left()));
            if (r.right() + 1 < w)
                memset(line + r.right() + 1, 0, size_t(w - r.right() - 1));
        }
        break;
    }
    }

    invalidateCache();
}

void Selection::combineShapeMask(const QImage &shapeMask, ChannelOp op, const QRect &boundsHint)
{
    if (m_mask.isNull() || shapeMask.isNull())
        return;
    if (shapeMask.size() != m_mask.size())
        return;

    const QRect area = boundsHint.isEmpty()
                           ? m_mask.rect()
                           : boundsHint.intersected(m_mask.rect());

    switch (op) {
    case ChannelOp::Replace:
        m_mask.fill(0);
        for (int y = area.top(); y <= area.bottom(); ++y) {
            const uchar *src = shapeMask.constScanLine(y);
            uchar *dst = m_mask.scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                if (src[x])
                    dst[x] = src[x]; // 保留软边（alpha→选区）
            }
        }
        break;
    case ChannelOp::Add:
        for (int y = area.top(); y <= area.bottom(); ++y) {
            const uchar *src = shapeMask.constScanLine(y);
            uchar *dst = m_mask.scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                if (src[x] > dst[x])
                    dst[x] = src[x];
            }
        }
        break;
    case ChannelOp::Subtract:
        for (int y = area.top(); y <= area.bottom(); ++y) {
            const uchar *src = shapeMask.constScanLine(y);
            uchar *dst = m_mask.scanLine(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                if (src[x] == 0)
                    continue;
                // soft：dst *= (1 - src/255)
                dst[x] = static_cast<uchar>((int(dst[x]) * (255 - src[x]) + 127) / 255);
            }
        }
        break;
    case ChannelOp::Intersect: {
        // 形状外清零；形状内取 min（对照 combine INTERSECT）
        const int w = m_mask.width();
        const int h = m_mask.height();
        for (int y = 0; y < h; ++y) {
            const uchar *src = shapeMask.constScanLine(y);
            uchar *dst = m_mask.scanLine(y);
            for (int x = 0; x < w; ++x)
                dst[x] = qMin(dst[x], src[x]);
        }
        break;
    }
    }

    invalidateCache();
}

void Selection::selectEllipse(const QRect &rect, ChannelOp op)
{
    // 【功能】对照 gimp_channel_select_ellipse → gimp_channel_combine_ellipse（硬边、无羽化）
    if (m_mask.isNull())
        return;

    const QRect r = rect.normalized().intersected(m_mask.rect());
    if (r.isEmpty()) {
        if (op == ChannelOp::Replace)
            clear();
        return;
    }

    // 用 ARGB 临时图画椭圆再抽亮度，避免 QPainter 直接画 Grayscale8 的兼容问题
    QImage argb(m_mask.size(), QImage::Format_ARGB32_Premultiplied);
    argb.fill(Qt::transparent);
    {
        QPainter painter(&argb);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(r);
    }

    QImage shape(m_mask.size(), QImage::Format_Grayscale8);
    shape.fill(0);
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        uchar *dst = shape.scanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            if (qAlpha(src[x]) > 127)
                dst[x] = 255;
        }
    }

    combineShapeMask(shape, op, r);
}

void Selection::selectFromLayerAlpha(const QImage &layerPremul,
                                     int offsetX, int offsetY,
                                     ChannelOp op)
{
    // 【功能】对照 gimp_channel_select_alpha：把 drawable 的 alpha 合入 selection_mask
    if (m_mask.isNull())
        return;

    QImage src = layerPremul;
    if (!src.isNull() && src.format() != QImage::Format_ARGB32_Premultiplied)
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage shape(m_mask.size(), QImage::Format_Grayscale8);
    shape.fill(0);

    if (src.isNull() || src.width() <= 0 || src.height() <= 0) {
        // 无像素 ≈ 全透明 alpha → Replace 则清空选区
        combineShapeMask(shape, op, QRect());
        return;
    }

    const QRect layerRect(offsetX, offsetY, src.width(), src.height());
    const QRect area = layerRect.intersected(m_mask.rect());
    if (area.isEmpty()) {
        if (op == ChannelOp::Replace)
            clear();
        return;
    }

    for (int docY = area.top(); docY <= area.bottom(); ++docY) {
        const int ly = docY - offsetY;
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(ly));
        uchar *dst = shape.scanLine(docY);
        for (int docX = area.left(); docX <= area.right(); ++docX) {
            const int lx = docX - offsetX;
            dst[docX] = static_cast<uchar>(qAlpha(line[lx]));
        }
    }

    combineShapeMask(shape, op, area);
}

void Selection::selectFromLayerGray(const QImage &gray,
                                    int offsetX, int offsetY,
                                    ChannelOp op)
{
    if (m_mask.isNull())
        return;

    QImage src = gray;
    if (!src.isNull() && src.format() != QImage::Format_Grayscale8)
        src = src.convertToFormat(QImage::Format_Grayscale8);

    QImage shape(m_mask.size(), QImage::Format_Grayscale8);
    shape.fill(0);

    if (src.isNull() || src.width() <= 0 || src.height() <= 0) {
        combineShapeMask(shape, op, QRect());
        return;
    }

    const QRect layerRect(offsetX, offsetY, src.width(), src.height());
    const QRect area = layerRect.intersected(m_mask.rect());
    if (area.isEmpty()) {
        if (op == ChannelOp::Replace)
            clear();
        return;
    }

    for (int docY = area.top(); docY <= area.bottom(); ++docY) {
        const int ly = docY - offsetY;
        const uchar *line = src.constScanLine(ly);
        uchar *dst = shape.scanLine(docY);
        for (int docX = area.left(); docX <= area.right(); ++docX) {
            const int lx = docX - offsetX;
            dst[docX] = line[lx];
        }
    }

    combineShapeMask(shape, op, area);
}

} // namespace Ps
