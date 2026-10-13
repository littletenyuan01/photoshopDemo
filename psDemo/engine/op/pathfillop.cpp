/**
 * pathfillop.cpp — PathFillOp 实现。
 */
#include "pathfillop.h"

#include <QPainter>
#include <QPainterPath>

namespace Ps {

QImage PathFillOp::rasterize(const Path &path, const QSize &docSize, Qt::FillRule rule)
{
    QImage mask(docSize, QImage::Format_Grayscale8);
    if (docSize.isEmpty())
        return mask;
    mask.fill(0);

    if (path.isEmpty())
        return mask;

    QPainterPath pp = path.toPainterPath();
    if (pp.isEmpty())
        return mask;
    pp.setFillRule(rule);

    // 【为什么走 ARGB 再抽 alpha】与 selectEllipse / SelectPolygonOp 同一口径：
    // 直接对 Grayscale8 画路径在部分平台上 brush 语义不可靠；ARGB+AA 能保留软边。
    QImage argb(docSize, QImage::Format_ARGB32_Premultiplied);
    argb.fill(Qt::transparent);
    {
        QPainter painter(&argb);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPath(pp);
    }

    const int w = docSize.width();
    const int h = docSize.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        uchar *dst = mask.scanLine(y);
        for (int x = 0; x < w; ++x)
            dst[x] = static_cast<uchar>(qAlpha(src[x]));
    }
    return mask;
}

} // namespace Ps
