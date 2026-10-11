/**
 * medianblurfilterop.cpp — 滑窗直方图中值模糊（engine/op 层）。
 */
#include "medianblurfilterop.h"

#include <QVector>

#include <cstring>

namespace Ps {

bool MedianBlurFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->medianRadius(), 8);
    return true; // 没有区域尺寸资源要申请：直方图是成员（恒 2 KB）
}

void MedianBlurFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    // 没有「不该跨调用保留」的资源：直方图恒 2 KB、与 ROI 无关，留着反而省掉每次分配；
    // 它的清理由 process 每行的 memset 负责。这里只复位参数。
    m_radius = 0;
}

void MedianBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    const int r = m_radius;
    const int w = image.width();
    const int h = image.height();
    if (r <= 0 || w <= 0 || h <= 0)
        return;

    // 1) 预乘 → 直通（与 BoxBlur / FindEdges 同口径）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    const int ringRows = 2 * r + 1;
    const int samples = ringRows * ringRows; // 窗口样本数（恒为奇数）
    const int half = (samples + 1) / 2;      // 「第 half 小」即中值

    // 只缓存窗口需要的 2r+1 行**原始（直通）**像素：内存 O(r·w)，不是整图。
    // 就地写回会污染后续要用的邻域，所以每行在**被覆盖之前**恰好装载一次；
    // 取样本一律走这个环形缓冲，绝不回头读 image（它正在被写）。
    QVector<QVector<QRgb>> ring(ringRows, QVector<QRgb>(w));
    const auto loadRow = [&](int y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        std::memcpy(ring[y % ringRows].data(), src, size_t(w) * sizeof(QRgb));
    };

    // abyss = 透明：越界样本按 0 计入（窗口样本数恒为 (2r+1)²，不做重归一化）
    const auto samp = [&](int x, int y, int c) -> int {
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0;
        const QRgb p = ring[y % ringRows][x];
        return (c == 0) ? qRed(p) : ((c == 1) ? qGreen(p) : ((c == 2) ? qBlue(p) : qAlpha(p)));
    };
    const auto medianOf = [half](const quint16 *hist) -> int {
        int cum = 0;
        for (int v = 0; v < 256; ++v) {
            cum += hist[v];
            if (cum >= half)
                return v;
        }
        return 255;
    };

    for (int y = 0; y <= r && y < h; ++y)
        loadRow(y);

    for (int y = 0; y < h; ++y) {
        if (y + r < h)
            loadRow(y + r); // 新进入窗口的最下一行（此刻它还没被写过）

        // x = 0 的窗口直方图（每行重建一次）
        std::memset(m_hist, 0, sizeof(m_hist));
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                for (int c = 0; c < 4; ++c)
                    ++m_hist[c][samp(dx, y + dy, c)];

        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            d[x] = qRgba(medianOf(m_hist[0]), medianOf(m_hist[1]),
                         medianOf(m_hist[2]), medianOf(m_hist[3]));
            // 滑到 x+1：新列 x+r+1 进入、旧列 x-r 离开（各 2r+1 个样本）
            const int addX = x + r + 1;
            const int delX = x - r;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = y + dy;
                for (int c = 0; c < 4; ++c) {
                    ++m_hist[c][samp(addX, yy, c)];
                    --m_hist[c][samp(delX, yy, c)];
                }
            }
        }
    }

    // 2) 直通 → 预乘（与第 1 步配对）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qPremultiply(row[x]);
    }
}

} // namespace Ps
