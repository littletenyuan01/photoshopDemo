/**
 * magneticedgesnap.h — 磁性套索格点边缘吸附（engine 层）。
 *
 * 【对照 GIMP】`gimpiscissorstool.c` + `gimptilehandleriscissors.c`：
 *   - find_max_gradient ≈ snapToGridCorner（局部最大梯度 + 近处加权）
 *   - find_optimal_path ≈ walkGridEdge（段间最短代价路径 / Livewire）
 * 本实现不做 GEGL tile 梯度缓存，用 Sobel 角点强度 + Dijkstra；
 * 搜索形状按 PS：Width 为半径的 **圆**（GIMP 固定方邻域 GRADIENT_SEARCH=32）。
 *
 * 代价项：f_G（梯度幅值）+ f_D（方向连续性），与 GIMP 的 OMEGA_G/OMEGA_D 两项对应；
 * **不做 f_Z**（拉普拉斯零交叉）——这一点与 GIMP 相同。
 *
 * 【与 GIMP 的差异（本项目取舍）】
 *   1. 方向项只取**目标节点**的梯度方向；GIMP 对 link 两端节点取平均
 *      （`calculate_link` 里 `direction_value[dir1][link] + direction_value[dir2][link]`）。
 *      沿一条边缘走时两端方向基本一致，差异可忽略；换来的是不必缓存每一点的搜索中方向。
 *   2. 用真正的 Dijkstra（优先队列），GIMP 是 ROI 内两趟松弛。
 *   3. 额外有「椭圆约束」：路径不许绕到光标前方（见 walkGridEdge）。
 */
#ifndef ENGINE_MAGNETICEDGESNAP_H
#define ENGINE_MAGNETICEDGESNAP_H

#include <QImage>
#include <QPointF>
#include <QVector>
#include <QtGlobal>

#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace Ps {
namespace MagneticEdgeSnap {

/** π（不自带 M_PI，避免依赖平台宏）。 */
constexpr qreal kPi = 3.14159265358979323846;

// ─────────────────────────────────────────────────────────────────────────────
// 标定：Sobel 幅值的量程
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 归一化常数：Sobel **单轴**满量程 = 4 × 255 = 1020
 * （核权重和 = 4，每轴最大 = 255+2×255+255）。
 *
 * 为什么不用理论 hypot 最大值 `hypot(1020,1020) ≈ 1442.5`：那个值只有理想棋盘格
 * （sx、sy 同时满量程）才可能达到，真实边缘永远够不着 —— 用它归一化等于白白浪费
 * 约 30% 量程（满对比度台阶也只到 180/255）。
 *
 * 取 1020 的好处：竖直/水平台阶边（mag = 4V）归一化后**恰好等于灰阶差 V**，
 * 于是 0..255 的量程被真实边缘用满，代价随边缘强度单调可分；
 * 极端斜边（>1020）被 qMin 夹到 255，无害。
 */
constexpr qreal kSobelMax = 255.0 * 4.0;

/**
 * 原始 Sobel 幅值 → 0..255。
 *
 * 【为什么必须归一化】对照 GIMP `gimptilehandleriscissors.c` 的梯度图：
 *     gradmap[0] = gradient * 255 / MAX_GRADIENT        // MAX_GRADIENT ≈ 179.6
 * GIMP 先把梯度铺满它自己的量程，后面的 `grad1 = 255 - grad1` 才有区分度
 * （注意 GIMP 的卷积结果被 int8 夹在 ±127，所以它的可用窗口其实只到 V≈32）。
 *
 * 本实现原先直接拿原始幅值再 `min(mag, 255)`：灰阶差 ≥ 64（≈25% 对比度）的边缘
 * mag 已 ≥ 256 → 全部压成同一档代价，等于把区分度丢光。代价图在强边上处处相等，
 * Dijkstra 只能靠优先队列 tie-break 选路 → 贴边不稳、在两条近邻边之间横跳。
 */
inline qreal normalizeMag(qreal rawMag)
{
    return qMin(255.0, rawMag * 255.0 / kSobelMax);
}

// ─────────────────────────────────────────────────────────────────────────────
// 方向项 f_D：让路径倾向于「沿着同一条边」而不是横穿
// ─────────────────────────────────────────────────────────────────────────────

/** 梯度方向在 [0, π) 上量化 8 档（边方向是 mod π 的，正反是同一条边）。 */
constexpr int kDirBins = 8;
/** 弱梯度 → 无方向。 */
constexpr int kDirNone = kDirBins;

/** 8 邻域移动方向对应的角度（顺序必须与 walkGridEdge 的 kDx/kDy 一致）。
 *  查表而非 atan2：本函数在热路径上（每个邻居一次）。 */
inline qreal linkAngle(int link)
{
    static const qreal kAng[8] = {
        -3.0 * kPi / 4.0, // (-1,-1)
        -kPi / 2.0,       // ( 0,-1)
        -kPi / 4.0,       // ( 1,-1)
        kPi,              // (-1, 0) 取 +π（cos 是偶函数，与 -π 等价）
        0.0,              // ( 1, 0)
        3.0 * kPi / 4.0,  // (-1, 1)
        kPi / 2.0,        // ( 0, 1)
        kPi / 4.0,        // ( 1, 1)
    };
    return kAng[link];
}

/**
 * 方向代价，语义对照 GIMP 的 `direction_value[dir][link]`：
 *   - 移动方向**垂直**于梯度方向（= 沿着边走）→ ≈ 0
 *   - 移动方向**平行**于梯度方向（= 横穿边）  → ≈ 255
 *   - 无方向（弱梯度）→ 255（GIMP 同样 `direction_value[255][*] = 255`）
 *
 * GIMP 用 `(127 - abs(127-i))*2` 之类构造 8×4 查表；这里等价地按 |cos| 建 8×8 表。
 * **必须查表**：热路径上每个被展开节点的每个邻居都要取一次
 * （约 3.7 万格点 × 8 ≈ 30 万次），现场算 cos 会明显拖慢拖拽。
 */
inline qreal dirPenalty(int dirBin, int link)
{
    if (dirBin == kDirNone)
        return 255.0;
    // 函数内 static：C++11 起初始化线程安全，且只算一次 64 个 cos
    static const auto kTable = [] {
        std::array<std::array<qreal, 8>, kDirBins> t{};
        for (int b = 0; b < kDirBins; ++b) {
            const qreal gradAng = (b + 0.5) * kPi / kDirBins;
            for (int k = 0; k < 8; ++k)
                t[size_t(b)][size_t(k)] = std::fabs(std::cos(linkAngle(k) - gradAng)) * 255.0;
        }
        return t;
    }();
    return kTable[size_t(dirBin)][size_t(link)];
}

namespace detail {

inline int grayAt(const QImage &source, int x, int y)
{
    const int w = source.width();
    const int h = source.height();
    x = qBound(0, x, w - 1);
    y = qBound(0, y, h - 1);
    const QRgb px = reinterpret_cast<const QRgb *>(source.constScanLine(y))[x];
    const int a = qAlpha(px);
    if (a <= 0)
        return 0;
    const int r = (qRed(px) * 255 + a / 2) / a;
    const int g = (qGreen(px) * 255 + a / 2) / a;
    const int b = (qBlue(px) * 255 + a / 2) / a;
    return (r * 30 + g * 59 + b * 11) / 100;
}

/** Sobel 梯度；同时给出 sx/sy（方向项要用）。 */
inline qreal sobelGrad(const QImage &source, int x, int y, int *sxOut, int *syOut)
{
    const int g00 = grayAt(source, x - 1, y - 1);
    const int g10 = grayAt(source, x, y - 1);
    const int g20 = grayAt(source, x + 1, y - 1);
    const int g01 = grayAt(source, x - 1, y);
    const int g21 = grayAt(source, x + 1, y);
    const int g02 = grayAt(source, x - 1, y + 1);
    const int g12 = grayAt(source, x, y + 1);
    const int g22 = grayAt(source, x + 1, y + 1);
    const int sx = (g20 + 2 * g21 + g22) - (g00 + 2 * g01 + g02);
    const int sy = (g02 + 2 * g12 + g22) - (g00 + 2 * g10 + g20);
    if (sxOut)
        *sxOut = sx;
    if (syOut)
        *syOut = sy;
    return std::hypot(qreal(sx), qreal(sy));
}

/** 只要幅值的入口。 */
inline qreal sobelMag(const QImage &source, int x, int y)
{
    return sobelGrad(source, x, y, nullptr, nullptr);
}

/** 梯度向量 → 量化档位（折到 [0, π)；零梯度 → kDirNone）。 */
inline int quantizeDir(int sx, int sy)
{
    if (sx == 0 && sy == 0)
        return kDirNone;
    qreal ang = std::atan2(qreal(sy), qreal(sx)); // (-π, π]
    if (ang < 0.0)
        ang += kPi; // (-π,0) → (0,π)
    if (ang >= kPi)
        ang -= kPi; // π → 0（同一条边）
    const int bin = int(ang * kDirBins / kPi);
    return qBound(0, bin, kDirBins - 1);
}

/** 格点 (i,j) 角点：邻接 4 像素中幅值最大者，给出其幅值与方向。 */
inline qreal cornerGrad(const QImage &source, int i, int j, int *dirOut)
{
    const int w = source.width();
    const int h = source.height();
    qreal best = 0.0;
    int bestSx = 0;
    int bestSy = 0;
    const int cands[4][2] = {{i - 1, j - 1}, {i, j - 1}, {i - 1, j}, {i, j}};
    for (const auto &c : cands) {
        const int px = c[0];
        const int py = c[1];
        if (px < 0 || py < 0 || px >= w || py >= h)
            continue;
        int sx = 0;
        int sy = 0;
        const qreal m = sobelGrad(source, px, py, &sx, &sy);
        if (m > best) {
            best = m;
            bestSx = sx;
            bestSy = sy;
        }
    }
    if (dirOut)
        *dirOut = quantizeDir(bestSx, bestSy);
    return best;
}

/** 只要幅值的入口（吸附走这条：只比大小、不做截断，本来就没问题）。 */
inline qreal cornerStrength(const QImage &source, int i, int j)
{
    return cornerGrad(source, i, j, nullptr);
}

/**
 * 节点代价（对照 GIMP `grad1 = 255 - grad1`）：强边便宜。
 *
 * ★ 幅值**先归一化**再算代价——这是修掉「强边代价饱和」的关键。
 * 弱边门槛 `minMag` 仍按**原始幅值**判定：不改动 Contrast 既有标定口径，
 * 否则同一个 Contrast 值会突然苛刻好几倍。
 * minMag 以下仍可走，但额外惩罚，避免完全断路。
 *
 * @param dirOut 可选输出：该节点的梯度方向档位（给 f_D 项用）
 */
inline float nodeCost(const QImage &source, int i, int j, qreal minMag, int *dirOut = nullptr)
{
    if (source.isNull()) {
        if (dirOut)
            *dirOut = kDirNone;
        return 128.f;
    }
    const qreal raw = cornerGrad(source, i, j, dirOut);
    const float base = float(255.0 - normalizeMag(raw)) + 5.0f; // 与旧式同量级：5..260
    return (raw < minMag) ? (base + 90.f) : base;
}

} // namespace detail

inline QPointF nearestGridCorner(const QPointF &cursor)
{
    return QPointF(std::round(cursor.x()), std::round(cursor.y()));
}

/**
 * 在 Width **圆形**邻域内的格点上，找 Contrast 以上、强度最大的角点。
 *
 * 【对照 GIMP】find_max_gradient（固定 GRADIENT_SEARCH 方邻域，
 * 权重 `distance_weights = 1/(1+距离)`）。
 *
 * 注意：这里用**原始幅值**比较（只比大小，量程不影响结果），
 * `minMag` 也按原始量程 —— 与 nodeCost 的归一化口径分开。
 */
inline QPointF snapToGridCorner(const QImage &source, const QPointF &cursor,
                                int radiusPx = 10, qreal minMag = 48.0,
                                qreal preferNear = 0.0)
{
    const QPointF fallback = nearestGridCorner(cursor);
    if (source.isNull() || source.width() < 1 || source.height() < 1)
        return fallback;

    const int w = source.width();
    const int h = source.height();
    const int R = qMax(1, radiusPx);
    const qreal r2 = qreal(R) * qreal(R);
    const int x0 = int(std::floor(cursor.x() - R));
    const int x1 = int(std::ceil(cursor.x() + R));
    const int y0 = int(std::floor(cursor.y() - R));
    const int y1 = int(std::ceil(cursor.y() + R));

    qreal bestScore = -1e300;
    int bestI = int(std::round(cursor.x()));
    int bestJ = int(std::round(cursor.y()));
    bool found = false;

    for (int j = y0; j <= y1; ++j) {
        for (int i = x0; i <= x1; ++i) {
            const qreal dx = qreal(i) - cursor.x();
            const qreal dy = qreal(j) - cursor.y();
            if (dx * dx + dy * dy > r2)
                continue;
            if (i < 0 || j < 0 || i > w || j > h)
                continue;

            const qreal mag = detail::cornerStrength(source, i, j);
            if (mag < minMag)
                continue;

            const qreal score = (preferNear <= 0.0)
                                    ? mag
                                    : (mag - preferNear * std::hypot(dx, dy));
            if (!found || score > bestScore) {
                found = true;
                bestScore = score;
                bestI = i;
                bestJ = j;
            }
        }
    }

    if (!found)
        return fallback;
    return QPointF(qreal(bestI), qreal(bestJ));
}

/**
 * 段间 Livewire：从 from 走到 to（不含起点、含终点）。
 *
 * 路径终点必须是 to（通常=光标格点）；椭圆约束禁止绕到终点前方。
 * 边代价 = 目标节点代价 × 步长（f_G 项） + 方向代价（f_D 项）。
 *
 * @param minMag    弱边门槛（**原始 Sobel 量程**，与 Contrast 口径一致）
 * @param omegaDir  f_D 项权重。默认 0.5 的来由：GIMP 里方向项上限/幅值项上限
 *                  ≈ 102/288 ≈ 0.35；本实现幅值项上限 ≈ 260×√2 ≈ 368，
 *                  按同比例反推方向项上限 ≈ 129 → 129/255 ≈ 0.5。
 *                  （比例来自 GIMP 常量推算，**未经实测标定**）
 */
inline QVector<QPointF> walkGridEdge(const QImage &source,
                                     const QPointF &from,
                                     const QPointF &to,
                                     qreal minMag = 0.0,
                                     qreal omegaDir = 0.5)
{
    QVector<QPointF> out;
    const int sx = int(std::round(from.x()));
    const int sy = int(std::round(from.y()));
    const int ex = int(std::round(to.x()));
    const int ey = int(std::round(to.y()));
    if (sx == ex && sy == ey)
        return out;

    const int imgW = source.isNull() ? 0 : source.width();
    const int imgH = source.isNull() ? 0 : source.height();
    const int maxX = qMax(0, imgW);
    const int maxY = qMax(0, imgH);

    // 包围盒只需覆盖起终点；少扩展以免绕到光标前方
    int x0 = qMin(sx, ex);
    int x1 = qMax(sx, ex);
    int y0 = qMin(sy, ey);
    int y1 = qMax(sy, ey);
    const int bw = x1 - x0 + 1;
    const int bh = y1 - y0 + 1;
    int padX = qMax(3, int(bw * 0.12) + 3);
    int padY = qMax(3, int(bh * 0.12) + 3);
    constexpr int kMaxSide = 160;
    if (bw + 2 * padX > kMaxSide)
        padX = qMax(2, (kMaxSide - bw) / 2);
    if (bh + 2 * padY > kMaxSide)
        padY = qMax(2, (kMaxSide - bh) / 2);

    x0 = qBound(0, x0 - padX, maxX);
    x1 = qBound(0, x1 + padX, maxX);
    y0 = qBound(0, y0 - padY, maxY);
    y1 = qBound(0, y1 + padY, maxY);

    const int W = x1 - x0 + 1;
    const int H = y1 - y0 + 1;
    if (W <= 0 || H <= 0) {
        out.append(QPointF(qreal(ex), qreal(ey)));
        return out;
    }

    const int N = W * H;
    auto idx = [W, x0, y0](int x, int y) {
        return (y - y0) * W + (x - x0);
    };
    auto inRoi = [x0, x1, y0, y1](int x, int y) {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    };

    constexpr float kInf = std::numeric_limits<float>::infinity();
    std::vector<float> dist(size_t(N), kInf);
    std::vector<int> parent(size_t(N), -1);

    const int startI = idx(qBound(x0, sx, x1), qBound(y0, sy, y1));
    const int goalX = qBound(x0, ex, x1);
    const int goalY = qBound(y0, ey, y1);
    const int goalI = idx(goalX, goalY);

    using Node = std::pair<float, int>; // cost, linear index
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> heap;
    dist[size_t(startI)] = 0.f;
    heap.push({0.f, startI});

    static const int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    static const float kStep[8] = {
        1.414f, 1.f, 1.414f, 1.f, 1.f, 1.414f, 1.f, 1.414f
    };

    // 椭圆约束：路径不得绕到「光标尚未到达」的前方（焦点=起终点）
    const float chord = std::hypot(float(ex - sx), float(ey - sy));
    const float major = chord + 6.f;    // 少许松弛以便贴边拐弯
    const float maxReach = chord + 2.f; // 相对起点不超过终点距离

    while (!heap.empty()) {
        const auto [cost, u] = heap.top();
        heap.pop();
        if (cost > dist[size_t(u)])
            continue;
        if (u == goalI)
            break;

        const int ux = x0 + (u % W);
        const int uy = y0 + (u / W);
        for (int k = 0; k < 8; ++k) {
            const int vx = ux + kDx[k];
            const int vy = uy + kDy[k];
            if (!inRoi(vx, vy))
                continue;
            const float dStart = std::hypot(float(vx - sx), float(vy - sy));
            if (dStart > maxReach)
                continue;
            const float dEnd = std::hypot(float(vx - ex), float(vy - ey));
            if (dStart + dEnd > major)
                continue;
            const int v = idx(vx, vy);
            // f_G 项：目标节点代价 × 步长（对照 GIMP link 上的梯度代价）
            int dirV = kDirNone;
            const float nodeC = detail::nodeCost(source, vx, vy, minMag, &dirV);
            const float stepCost =
                nodeC * kStep[k]
                // f_D 项：沿边走≈0 / 横穿边≈255（对照 GIMP direction_value × OMEGA_D）
                + float(omegaDir) * float(dirPenalty(dirV, k));
            const float nd = dist[size_t(u)] + stepCost;
            if (nd < dist[size_t(v)]) {
                dist[size_t(v)] = nd;
                parent[size_t(v)] = u;
                heap.push({nd, v});
            }
        }
    }

    if (!std::isfinite(dist[size_t(goalI)])) {
        // 失败时退回直线格点
        int cx = sx;
        int cy = sy;
        while (cx != ex || cy != ey) {
            if (cx < ex)
                ++cx;
            else if (cx > ex)
                --cx;
            if (cy < ey)
                ++cy;
            else if (cy > ey)
                --cy;
            out.append(QPointF(qreal(cx), qreal(cy)));
        }
        return out;
    }

    // 回溯：goal → start，再反转；丢掉起点
    QVector<QPointF> rev;
    for (int cur = goalI; cur >= 0; cur = parent[size_t(cur)]) {
        const int px = x0 + (cur % W);
        const int py = y0 + (cur / W);
        rev.append(QPointF(qreal(px), qreal(py)));
        if (cur == startI)
            break;
    }
    for (int i = rev.size() - 2; i >= 0; --i)
        out.append(rev[i]);

    // 若 ROI 钳制导致终点被夹，补上真实终点
    if (out.isEmpty()
        || int(std::round(out.last().x())) != ex
        || int(std::round(out.last().y())) != ey) {
        out.append(QPointF(qreal(ex), qreal(ey)));
    }
    return out;
}

} // namespace MagneticEdgeSnap
} // namespace Ps

#endif // ENGINE_MAGNETICEDGESNAP_H
