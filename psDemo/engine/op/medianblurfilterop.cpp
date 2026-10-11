/**
 * medianblurfilterop.cpp — 滑窗直方图中值模糊（engine/op 层）。
 *
 * 定位中值：**增量 cum**（不再每像素扫 256 bin）。详见头文件与本文件内注释。
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
    //
    // 【为什么逐行 append，而不是 `QVector<QVector<QRgb>> ring(n, QVector<QRgb>(w))`】
    // 带「重复值」的容器构造会让各元素**共享同一缓冲区**（写时才 detach），此时
    // `constData()` 对每一行返回**同一个地址**。若代码里还有第二条按行取槽的路径
    // （本项目曾同时存在 `samp()` 与 `slotOf()` 两版），就会「建立直方图读一行、滑窗更新
    // 读另一行」，把 bin 减穿成 quint16 回绕（实测直方图和 65561/25 —— 即「和 ≡ 25 (mod 2^16)
    // 但内容全错」）。逐行建 + 只保留 rowAt() 一条取行路径，从根上避免。
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

    for (int y = 0; y <= r && y < h; ++y)
        loadRow(y);

    for (int y = 0; y < h; ++y) {
        // 协作式取消：中值是本族最重的算子（512² r=3 ≈49 ms），逐行查一次
        // （见 filterop.h 的 FilterContext::shouldAbort）。取消后立即返回、不保证输出完整。
        if (aborted(ctx))
            return;
        if (y + r < h)
            loadRow(y + r); // 新进入窗口的最下一行（此刻它还没被写过）

        // x = 0 的窗口直方图（每行重建一次）
        std::memset(m_hist, 0, sizeof(m_hist));
        for (int dy = -r; dy <= r; ++dy) {
            const QRgb *line = rowAt(y + dy);
            for (int dx = -r; dx <= r; ++dx)
                for (int c = 0; c < 4; ++c) {
                    int v = 0;
                    if (line && dx >= 0 && dx < w) {
                        const QRgb q = line[dx];
                        v = (c == 0) ? qRed(q)
                                     : ((c == 1) ? qGreen(q) : ((c == 2) ? qBlue(q) : qAlpha(q)));
                    }
                    ++m_hist[c][v]; // 越界样本按 0 计入，窗口样本数恒为 (2r+1)²
                }
        }

        // 每行开头复位「增量 cum 定位」状态：直方图刚重建，没有可继承的位置。
        // 成本 O(256)/行/通道（每行一次），相对逐像素扫 256 可忽略。
        for (int c = 0; c < 4; ++c) {
            int cum = 0;
            int p = 0;
            while (p < 255 && cum + m_hist[c][p] < half) {
                cum += m_hist[c][p];
                ++p;
            }
            m_pos[c] = p;
            m_posCum[c] = cum;
        }

        QRgb *d = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // 本像素滑到下一格：新列 x+r+1 进窗口、旧列 x-r 离开（各 2r+1 个样本）
            const int addX = x + r + 1;
            const int delX = x - r;
            const bool addIn = (addX < w);
            const bool delIn = (delX >= 0);
            const bool slid = addIn || delIn;

            int out[4];
            for (int c = 0; c < 4; ++c) {
                // ① 先取值（本像素的中值 = 当前不变量成立处 p）；② 补滑窗并按进出样本修正 cum；
                // ③ 把 p 移回不变量 `cum < half ≤ cum + hist[p]` 成立处（每格 ≤ 2r+1 步）。
                out[c] = m_pos[c];

                if (slid) {
                    for (int dy = -r; dy <= r; ++dy) {
                        const QRgb *line = rowAt(y + dy);
                        // 【口径必须守恒】进出样本各 (2r+1) 个，越界的一律按 0 计数：
                        // 直方图每像素的和恒为 (2r+1)²，滑窗才不漂。越界样本既不「少加」也不「少减」，
                        // 否则某个方向的越界会「只进不出」（实测直方图和 35/25），增量 cum 立刻失准。
                        const QRgb qAdd = (addIn && line) ? line[addX] : 0u;
                        const QRgb qDel = (delIn && line) ? line[delX] : 0u;

                        const int vAdd = (c == 0) ? qRed(qAdd)
                                                  : ((c == 1) ? qGreen(qAdd)
                                                              : ((c == 2) ? qBlue(qAdd) : qAlpha(qAdd)));
                        ++m_hist[c][vAdd];
                        if (vAdd < m_pos[c])
                            ++m_posCum[c];

                        const int vDel = (c == 0) ? qRed(qDel)
                                                  : ((c == 1) ? qGreen(qDel)
                                                              : ((c == 2) ? qBlue(qDel) : qAlpha(qDel)));
                        --m_hist[c][vDel];
                        if (vDel < m_pos[c])
                            --m_posCum[c];
                    }
                }

                int p = m_pos[c];
                while (p < 255 && m_posCum[c] + m_hist[c][p] < half) {
                    m_posCum[c] += m_hist[c][p];
                    ++p;
                }
                // 下界必须守住：中值**恰好在 bin 0** 时满足 `cum = 0 < half ≤ cum + hist[0]`，
                // 此时不能再往下走（`--p` 会变 −1，越界写 m_hist，后续像素全错）。
                // 旧实现每像素「从 bin 0 往上扫」天然不会越界，改成增量定位后必须显式加这个界限
                // （本轮实测：40×32 图自 y=0 起连续 154 个像素被判成 0）。
                while (p > 0 && m_posCum[c] >= half) {
                    --p;
                    m_posCum[c] -= m_hist[c][p];
                }
                Q_ASSERT(p >= 0 && p < 256 && m_posCum[c] >= 0 && m_posCum[c] < half);
                m_pos[c] = p;
            }
            d[x] = qRgba(out[0], out[1], out[2], out[3]);
        }
    }

    // 2) 直通 → 预乘（与第 1 步配对）
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = qPremultiply(row[x]);
    }
}

int MedianBlurFilterOp::locateMedianByScan(int channel) const
{
    if (channel < 0 || channel >= 4)
        return -1;
    const int half = (m_radius * 2 + 1) * (m_radius * 2 + 1) / 2 + 1;
    int cum = 0;
    for (int v = 0; v < 256; ++v) {
        cum += m_hist[channel][v];
        if (cum >= half)
            return v;
    }
    return 255;
}

} // namespace Ps
