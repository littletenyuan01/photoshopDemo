/**
 * selectpolygonop.cpp — SelectPolygonOp 实现（engine/op 层）。
 *
 * 光栅化闭合多边形 → Grayscale8 shapeMask → Selection::combineShapeMask。
 * 【对照 GIMP】gimp_channel_select_polygon + gimp_scan_convert_add_polyline(closed)。
 */
#include "selectpolygonop.h"

#include "domain/selection.h"

#include <QPainter>
#include <QtMath>

namespace Ps {

bool SelectPolygonOp::prepare(OpContext &ctx)
{
    // 选区算子不依赖 tiles；顶点不足无法成面
    if (!ctx.selection || ctx.selection->mask().isNull())
        return false;
    if (m_points.size() < 3)
        return false;
    return true;
}

QRect SelectPolygonOp::process(OpContext &ctx)
{
    Selection *sel = ctx.selection;
    const QSize size = sel->mask().size();

    // 外接矩形：限制抽亮度扫描范围（对照 GIMP scan convert 的 ROI 思路）
    const QRectF boundsF = m_points.boundingRect();
    QRect area = boundsF.toAlignedRect().intersected(QRect(QPoint(0, 0), size));
    if (area.isEmpty()) {
        if (m_op == ChannelOp::Replace)
            sel->clear();
        return {};
    }
    // 扩 1px，避免取整把边界点裁掉
    area = area.adjusted(-1, -1, 1, 1).intersected(QRect(QPoint(0, 0), size));

    // 与 selectEllipse 相同：ARGB 临时图再抽 alpha，避免直接画 Grayscale8 的兼容问题
    QImage argb(size, QImage::Format_ARGB32_Premultiplied);
    argb.fill(Qt::transparent);
    {
        QPainter painter(&argb);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPolygon(m_points, Qt::OddEvenFill);
    }

    QImage shape(size, QImage::Format_Grayscale8);
    shape.fill(0);
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        uchar *dst = shape.scanLine(y);
        for (int x = area.left(); x <= area.right(); ++x) {
            if (qAlpha(src[x]) > 127)
                dst[x] = 255;
        }
    }

    sel->combineShapeMask(shape, m_op, area);
    return area;
}

} // namespace Ps
