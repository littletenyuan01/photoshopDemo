/**
 * gaussianblurfilterop.cpp — 三次盒模糊逼近高斯（engine/op 层）。
 */
#include "gaussianblurfilterop.h"

#include <cmath>

namespace Ps {

void GaussianBlurFilterOp::boxRadiiFor(int radius, int radii[3])
{
    const double sigma = radius / 3.0; // UI 半径 → σ（口径见头文件）
    const int n = 3;

    const double wIdeal = std::sqrt((12.0 * sigma * sigma / n) + 1.0);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0)
        --wl; // 盒宽取奇数，滑动窗口才对称
    if (wl < 1)
        wl = 1;
    const int wu = wl + 2;

    // 用二次拟合决定「前几趟用 wl、其余用 wu」，使三趟的总方差最接近 σ²
    const double mIdeal = (12.0 * sigma * sigma - double(n) * wl * wl - 4.0 * n * wl - 3.0 * n)
                          / (-4.0 * wl - 4.0);
    int m = int(std::round(mIdeal));
    m = qBound(0, m, n);

    for (int i = 0; i < n; ++i) {
        const int w = (i < m) ? wl : wu;
        radii[i] = (w - 1) / 2;
    }
}

void GaussianBlurFilterOp::process(QImage &image, const FilterContext &ctx)
{
    if (image.isNull())
        return;
    const int radius = ctx.node ? ctx.node->gaussRadius() : 0;
    if (radius <= 0)
        return;

    int radii[3] = {0, 0, 0};
    boxRadiiFor(radius, radii);

    // 三趟盒模糊：每趟都交给盒模糊算子（它自己管理临时缓冲与直通↔预乘转换）。
    // 注意三趟是**连续**作用于同一张图，故中间结果的 alpha 语义与单趟一致。
    FilterOp *box = filterOpInstance(OpName::BoxBlur);
    if (!box)
        return; // 盒模糊未注册（漏调 opsInit）：宁可不处理，也不产出半成品
    for (int i = 0; i < 3; ++i) {
        if (radii[i] <= 0)
            continue; // 盒宽 1 = 恒等，跳过（半径 1 时三趟可能全为恒等）
        FilterNode blurNode(OpName::BoxBlur);
        blurNode.setBlurRadius(radii[i]);
        FilterContext bctx;
        bctx.node = &blurNode;
        bctx.size = image.size();
        if (box->prepare(bctx))
            box->process(image, bctx);
        box->finish(bctx);
    }
}

} // namespace Ps
