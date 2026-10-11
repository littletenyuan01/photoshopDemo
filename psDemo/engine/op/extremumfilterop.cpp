/**
 * extremumfilterop.cpp — 最大值/最小值实现（engine/op 层）。
 */
#include "extremumfilterop.h"

#include <QVector>

#include <cstring>

namespace Ps {

bool ExtremumFilterOp::prepare(const FilterContext &ctx)
{
    if (!ctx.node || ctx.size.isEmpty())
        return false;
    m_radius = qBound(1, ctx.node->extremumRadius(), 8);
    return true; // 没有区域尺寸资源要申请：直方图是成员（恒 2 KB）
}

void ExtremumFilterOp::finish(const FilterContext &ctx)
{
    Q_UNUSED(ctx);
    m_radius = 0;
}

void ExtremumFilterOp::process(QImage &image, const FilterContext &ctx)
{
    const int r = m_radius;
    const int w = image.width();
    const int h = image.height();
    if (r <= 0 || w <= 0 || h <= 0)
        return;

    // 1) 预乘 → 直通（与中值同口径）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qUnpremultiply(row[x]);
    }

    const int ringRows = 2 * r + 1;
    // 逐行 append（见 MedianBlurFilterOp：重复值构造会让各行共享缓冲）
    QVector<QVector<QRgb>> ring;
    ring.reserve(ringRows);
    for (int i = 0; i < ringRows; ++i)
        ring.append(QVector<QRgb>(w));

    const auto loadRow = [&](int y) {
        std::memcpy(ring[y % ringRows].data(), image.constScanLine(y), size_t(w) * sizeof(QRgb));
    };
    // 窗口内某一行（可为越界行）的起始像素；越界返回 nullptr，样本按 abyss = 透明（0）。
    const auto rowAt = [&](int row) -> const QRgb * {
        if (row < 0 || row >= h)
            return nullptr;
        return ring[row % ringRows].constData();
    };
    const auto ch = [](QRgb p, int c) -> int {
        return (c == 0) ? qRed(p) : ((c == 1) ? qGreen(p) : ((c == 2) ? qBlue(p) : qAlpha(p)));
    };

    for (int y = 0; y <= r && y < h; ++y)
        loadRow(y);

    for (int y = 0; y < h; ++y) {
        if (aborted(ctx))
            return; // 协作式取消（见 filterop.h）
        if (y + r < h)
            loadRow(y + r);

        // x = 0 的窗口直方图（每行重建一次）。**越界样本按 0 计入**（与中值/盒模糊同口径）：
        //  · 最小值：0 是合法下界，边缘窗口因此被拉向 0（= 层外透明）；
        //  · 最大值：0 不会抬高极值（总有 ≥1 个在界样本），故加不加 0 结果一样。
        std::memset(m_hist, 0, sizeof(m_hist));
        for (int dy = -r; dy <= r; ++dy) {
            const QRgb *line = rowAt(y + dy);
            for (int dx = -r; dx <= r; ++dx)
                for (int c = 0; c < 4; ++c) {
                    int v = 0;
                    if (line && dx >= 0 && dx < w)
                        v = ch(line[dx], c);
                    ++m_hist[c][v];
                }
        }

        // 极值复位：按直方图扫一次得到初始极值 bin
        for (int c = 0; c < 4; ++c) {
            if (m_mode == Mode::Maximum) {
                for (int v = 255; v >= 0; --v) {
                    if (m_hist[c][v] > 0) {
                        m_ext[c] = quint8(v);
                        break;
                    }
                }
            } else {
                for (int v = 0; v < 256; ++v) {
                    if (m_hist[c][v] > 0) {
                        m_ext[c] = quint8(v);
                        break;
                    }
                }
            }
        }

        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            d[x] = qRgba(m_ext[0], m_ext[1], m_ext[2], m_ext[3]);

            // 滑到 x+1：新列 x+r+1 进、旧列 x-r 出。
            // 【口径必须守恒】进出样本各 (2r+1) 个，越界的列/行**按 0 照常加减** ——
            // 否则「左边缘的 abyss=0」只进不出，直方图里的 0 越积越多，整行最小值被拉成 0
            // （本轮实测：最小值 oracle 差 1008 = 恰好整幅内部像素，最大值因忽略 0 而幸免）。
            const int addX = x + r + 1;
            const int delX = x - r;
            const bool addIn = (addX < w);
            const bool delIn = (delX >= 0);
            for (int dy = -r; dy <= r; ++dy) {
                const QRgb *line = rowAt(y + dy);
                for (int c = 0; c < 4; ++c) {
                    const int vAdd = (addIn && line) ? ch(line[addX], c) : 0;
                    ++m_hist[c][vAdd];
                    if (m_mode == Mode::Maximum) {
                        if (vAdd > m_ext[c])
                            m_ext[c] = quint8(vAdd);
                    } else if (vAdd < m_ext[c]) {
                        m_ext[c] = quint8(vAdd);
                    }

                    const int vDel = (delIn && line) ? ch(line[delX], c) : 0;
                    --m_hist[c][vDel];
                    if (vDel == m_ext[c] && m_hist[c][vDel] == 0) {
                        // 极值刚被移出窗口：向另一侧回退找新的极值
                        if (m_mode == Mode::Maximum) {
                            while (m_ext[c] > 0 && m_hist[c][m_ext[c]] == 0)
                                --m_ext[c];
                        } else {
                            while (m_ext[c] < 255 && m_hist[c][m_ext[c]] == 0)
                                ++m_ext[c];
                        }
                    }
                }
            }
        }
    }

    // 2) 直通 → 预乘
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qPremultiply(row[x]);
    }
}

} // namespace Ps
