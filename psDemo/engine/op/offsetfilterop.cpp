/**
 * offsetfilterop.cpp — 位移实现（engine/op 层）。
 *
 * 语义（对照 PS「位移」的「未定义区域 = 设置为背景/透明」一档）：
 * 对话框里的「水平/垂直」为**正值时图像内容向右/向下移**，即 `out(x,y) = in(x - dx, y - dy)`；
 * **移出层范围的内容丢弃、空出的部分透明**（层外本来就该是透明的）。
 *
 * 【为什么不做环绕】环绕是**多矩形**依赖（输出最左边要读输入最右边），单矩形表达不了，
 * 于是整条 ROI 机制都得为它退化成「整层求值一次再裁」—— 实测 4096×4096 文档只看 800×600 时
 * 要 74 ms，而不做环绕只要 2 ms（37×）。PS 的默认也不是环绕（默认「设置为背景」）。
 * 见 docs/pending-dev.md 的「位移默认改成非环绕」一节。
 *
 * 实现：另开一张结果图，逐行只搬与源行相交的那一段（一次 memcpy）。
 * 用 `constScanLine` 直读、不就地写 —— 读的位置与写的位置不重叠，不存在就地污染。
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
    // dx/dy 都是 0 时无事可做（也让「加了滤镜但没动」不产生无谓的整层拷贝）
    return m_dx != 0 || m_dy != 0;
}

void OffsetFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_dx = 0;
    m_dy = 0;
}

void OffsetFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    if (w <= 0 || h <= 0 || (m_dx == 0 && m_dy == 0))
        return;

    // 另开一张结果图：直接对 image 就地写会覆盖还没读的源像素。空出的部分保持透明。
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const int px = int(sizeof(QRgb));
    for (int y = 0; y < h; ++y) {
        const int srcY = y - m_dy;
        if (srcY < 0 || srcY >= h)
            continue; // 该行整行移出：保持透明

        const uchar *srcRow = image.constScanLine(srcY);
        uchar *dstRow = out.scanLine(y);

        // 只有与源行相交的那一段被搬过来：[max(0,dx), min(w, w+dx))
        const int x0 = qMax(0, m_dx);
        const int x1 = qMin(w, w + m_dx);
        if (x1 <= x0)
            continue;
        std::memcpy(dstRow + x0 * px, srcRow + (x0 - m_dx) * px, size_t(x1 - x0) * sizeof(QRgb));
    }

    image = out;
    Q_UNUSED(ctx);
}

} // namespace Ps
