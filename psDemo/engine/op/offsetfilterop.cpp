/**
 * offsetfilterop.cpp — 位移实现（engine/op 层）。
 *
 * 语义（对照 PS「位移」对话框）：对话框里的「水平/垂直」为**正值时图像内容向右/向下移**。
 * 即 `out(x,y) = in(x - dx, y - dy)`。
 *
 * 实现上只做一次按行搬运：对每个输出行 y，源行 = y - dy；wrap 时取模，
 * 否则越界行留空。行内再按 dx 分两段 memcpy（wrap）或一段（透明）。
 * 不分配整图临时缓冲 —— 用 `constScanLine` 直读、另开一张结果图写出，
 * 故「读的位置」与「写的位置」不重叠，不存在就地读写的污染问题。
 */
#include "offsetfilterop.h"

#include <cstring>

namespace Ps {

bool OffsetFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_dx = ctx.node->offsetDx();
    m_dy = ctx.node->offsetDy();
    m_wrap = ctx.node->offsetWrap();
    // dx/dy 都是 0 时无事可做（也让「加了滤镜但没动」不产生无谓的整层拷贝）
    return m_dx != 0 || m_dy != 0;
}

void OffsetFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_dx = 0;
    m_dy = 0;
    m_wrap = true;
}

void OffsetFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || (m_dx == 0 && m_dy == 0))
        return;

    // 用**另开一张**结果图：直接对 image 就地写会覆盖还没读的源像素
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    if (m_wrap)
        out.fill(Qt::transparent); // 环绕时每行都会被写满，但先清一次更稳
    else
        out.fill(Qt::transparent); // 非环绕：空出的部分就是透明

    const int bytesRow = w * int(sizeof(QRgb));
    for (int y = 0; y < h; ++y) {
        int srcY = y - m_dy;
        if (m_wrap) {
            srcY %= h;
            if (srcY < 0)
                srcY += h;
        } else if (srcY < 0 || srcY >= h) {
            continue; // 该行整行在外：保持透明
        }

        const uchar *srcRow = image.constScanLine(srcY);
        uchar *dstRow = out.scanLine(y);

        if (!m_wrap) {
            // 只有与源行相交的那一段被搬过来：[max(0,dx), min(w, w+dx))
            const int x0 = qMax(0, m_dx);
            const int x1 = qMin(w, w + m_dx);
            if (x1 <= x0)
                continue;
            std::memcpy(dstRow + x0 * int(sizeof(QRgb)), srcRow + (x0 - m_dx) * int(sizeof(QRgb)),
                        size_t(x1 - x0) * sizeof(QRgb));
            continue;
        }

        // 环绕：dst[x] = src[(x - dx) mod w]，等价于把源行右移 dx 后环绕
        int dx = m_dx % w;
        if (dx < 0)
            dx += w;
        // 第一段：dst[dx .. w) ← src[0 .. w-dx)
        const int tail = w - dx;
        if (tail > 0)
            std::memcpy(dstRow + dx * int(sizeof(QRgb)), srcRow, size_t(tail) * sizeof(QRgb));
        // 第二段：dst[0 .. dx) ← src[w-dx .. w)
        if (dx > 0)
            std::memcpy(dstRow, srcRow + tail * int(sizeof(QRgb)),
                        size_t(dx) * sizeof(QRgb));
    }
    Q_UNUSED(bytesRow);

    image = out;
    Q_UNUSED(ctx);
}

} // namespace Ps
