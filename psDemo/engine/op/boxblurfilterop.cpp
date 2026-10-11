/**
 * boxblurfilterop.cpp — 盒模糊实现（engine/op 层）。
 *
 * 算法要点（含两次「先量后改」得到的结论，详见 docs/pending-dev.md）：
 *  1. 先转**直通 alpha** 再逐通道卷积，最后乘回预乘 —— 避免在预乘像素上直滤产生透明边缘暗边；
 *  2. 归一化用**倒数乘法**而非整数除法（除数 n=2r+1 是运行时值，整数除法约 20~40 周期/次）；
 *     sum ≤ 65k、n ≤ 257 的范围内该近似**精确**，已由语义 oracle 证实与整数除法逐像素一致；
 *  3. 垂直趟逐样本取**行首指针**（不用 QImage::pixel() 的格式分发/边界检查）；
 *  4. 垂直趟按**图高自适应**：h 大时按列块（running sums 常驻 L1、每行读连续），
 *     h 小时逐列扫（标量累加，每像素成本最低）。
 */
#include "boxblurfilterop.h"

#include <QVector>
#include <QtGlobal>

namespace Ps {

bool BoxBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    const int r = ctx.node->blurRadius();
    if (r <= 0)
        return false; // 半径 0 = 无操作：跳过 process，但 finish 仍会被调用
    m_radius = qBound(0, r, 128);

    // 资源申请：按 ROI 尺寸准备两块缓冲；尺寸不变则复用（常驻实例的意义所在）
    if (m_cached != ctx.size) {
        m_straight = QImage(ctx.size, QImage::Format_ARGB32);
        m_tmp = QImage(ctx.size, QImage::Format_ARGB32);
        m_cached = ctx.size;
    }
    m_oversized = qint64(ctx.size.width()) * ctx.size.height() > kMaxRetainedPixels;
    return !m_straight.isNull() && !m_tmp.isNull();
}

void BoxBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    // 释放「不该跨调用保留」的资源：超大缓冲放掉，下次按需再申请。
    if (m_oversized) {
        m_straight = QImage();
        m_tmp = QImage();
        m_cached = QSize();
        m_oversized = false;
    }
    m_radius = 0;
}

void BoxBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int w = image.width();
    const int h = image.height();
    const int radius = m_radius;
    if (radius <= 0 || w <= 0 || h <= 0)
        return;

    // 防御：prepare 未跑或尺寸不符时退回本地缓冲（正确性优先，只是慢一点）
    QImage localA, localB;
    QImage *pStraight = &m_straight;
    QImage *pTmp = &m_tmp;
    if (m_straight.size() != image.size() || m_tmp.size() != image.size()) {
        localA = QImage(image.size(), QImage::Format_ARGB32);
        localB = QImage(image.size(), QImage::Format_ARGB32);
        pStraight = &localA;
        pTmp = &localB;
    }
    QImage &straight = *pStraight;
    QImage &tmp = *pTmp;

    const int n = radius * 2 + 1; // 盒宽
    const quint32 half = quint32(n / 2);
    const quint32 inv = quint32(0xFFFFFFFFull / quint32(n)) + 1u;
    const auto norm = [inv, half](quint32 sum) -> quint32 {
        return quint32((quint64(sum + half) * inv) >> 32);
    };

    // 1) 预乘 → 直通
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return; // 协作式取消：每 32 行查一次（见 filterop.h 的 FilterContext::shouldAbort）
        const QRgb *s = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(straight.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qUnpremultiply(s[x]);
    }

    // 2) 水平趟 → tmp
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(tmp.scanLine(y));
        quint32 r = 0, g = 0, b = 0, a = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int x = k;
            if (x < 0 || x >= w)
                continue;
            r += qRed(s[x]); g += qGreen(s[x]); b += qBlue(s[x]); a += qAlpha(s[x]);
        }
        for (int x = 0; x < w; ++x) {
            d[x] = qRgba(norm(r), norm(g), norm(b), norm(a));
            const int add = x + radius + 1;
            const int del = x - radius;
            if (add < w) { r += qRed(s[add]); g += qGreen(s[add]); b += qBlue(s[add]); a += qAlpha(s[add]); }
            if (del >= 0) { r -= qRed(s[del]); g -= qGreen(s[del]); b -= qBlue(s[del]); a -= qAlpha(s[del]); }
        }
    }

    // 3) 垂直趟 → straight（先取行首地址：避免逐样本 pixel()/scanLine() 的开销）
    QVector<const QRgb *> srcRows(h);
    QVector<QRgb *> dstRows(h);
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        srcRows[y] = reinterpret_cast<const QRgb *>(tmp.constScanLine(y));
        dstRows[y] = reinterpret_cast<QRgb *>(straight.scanLine(y));
    }

    // 两种走法按**图高**自适应（都是 O(w·h)，差别在「每像素成本」vs「访存局部性」）：
    //  · 逐列扫：每像素只有标量寄存器累加（成本最低），但工作集是 h 行×64B 的缓存行；
    //    h×64B 仍在 L1（~32KB，即 h ≲ 512）时它更快。
    //  · 按列块：每像素要读写 4 个 running-sum 数组（成本更高），但每行读/写连续、sums 常驻 L1；
    //    h 大时局部性收益压过成本（实测 h=1024：23.9 → 15.1 ms）。
    constexpr int kColBlock = 64;
    constexpr int kBlockedMinRows = 512;
    if (h > kBlockedMinRows) {
        QVector<quint32> sumR(kColBlock), sumG(kColBlock), sumB(kColBlock), sumA(kColBlock);
        for (int x0 = 0; x0 < w; x0 += kColBlock) {
            const int bw = qMin(kColBlock, w - x0);
            for (int i = 0; i < bw; ++i)
                sumR[i] = sumG[i] = sumB[i] = sumA[i] = 0;
            for (int k = -radius; k <= radius; ++k) {
                if (k < 0 || k >= h)
                    continue;
                const QRgb *row = srcRows[k] + x0;
                for (int i = 0; i < bw; ++i) {
                    const QRgb p = row[i];
                    sumR[i] += qRed(p); sumG[i] += qGreen(p);
                    sumB[i] += qBlue(p); sumA[i] += qAlpha(p);
                }
            }
            for (int y = 0; y < h; ++y) {
                if ((y & 31) == 0 && aborted(ctx))
                    return;
                QRgb *drow = dstRows[y] + x0;
                for (int i = 0; i < bw; ++i)
                    drow[i] = qRgba(norm(sumR[i]), norm(sumG[i]), norm(sumB[i]), norm(sumA[i]));
                const int add = y + radius + 1;
                const int del = y - radius;
                if (add < h) {
                    const QRgb *row = srcRows[add] + x0;
                    for (int i = 0; i < bw; ++i) {
                        const QRgb p = row[i];
                        sumR[i] += qRed(p); sumG[i] += qGreen(p);
                        sumB[i] += qBlue(p); sumA[i] += qAlpha(p);
                    }
                }
                if (del >= 0) {
                    const QRgb *row = srcRows[del] + x0;
                    for (int i = 0; i < bw; ++i) {
                        const QRgb p = row[i];
                        sumR[i] -= qRed(p); sumG[i] -= qGreen(p);
                        sumB[i] -= qBlue(p); sumA[i] -= qAlpha(p);
                    }
                }
            }
        }
    } else {
        for (int x = 0; x < w; ++x) {
            quint32 r = 0, g = 0, b = 0, a = 0;
            for (int k = -radius; k <= radius; ++k) {
                if (k < 0 || k >= h)
                    continue;
                const QRgb p = srcRows[k][x];
                r += qRed(p); g += qGreen(p); b += qBlue(p); a += qAlpha(p);
            }
            for (int y = 0; y < h; ++y) {
                if ((y & 31) == 0 && aborted(ctx))
                    return;
                dstRows[y][x] = qRgba(norm(r), norm(g), norm(b), norm(a));
                const int add = y + radius + 1;
                const int del = y - radius;
                if (add < h) { const QRgb p = srcRows[add][x]; r += qRed(p); g += qGreen(p); b += qBlue(p); a += qAlpha(p); }
                if (del >= 0) { const QRgb p = srcRows[del][x]; r -= qRed(p); g -= qGreen(p); b -= qBlue(p); a -= qAlpha(p); }
            }
        }
    }

    // 4) 直通 → 预乘写回
    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && aborted(ctx))
            return;
        const QRgb *s = reinterpret_cast<const QRgb *>(straight.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            d[x] = qPremultiply(s[x]);
    }
}

} // namespace Ps
