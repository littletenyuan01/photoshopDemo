/**
 * t_pixelprim.cpp — 逐像素合成原语：新实现 vs 改动前旧实现，穷举位等价。
 *
 * 背景：compositePixelOnto 为 BlendMode::Normal 加了内联快路径（省掉跨 TU 调用，
 * 并把 Normal 下恒为 0 的 (comp/255−cs) 项消掉）。这里逐字保留旧实现做对照，
 * 穷举 模式 × 像素对 × 不透明度 × 蒙版 × 坐标，确保是**位等价**而非近似。
 */
#include "domain/blendmode.h"
#include "engine/compositepixel.h"
#include "testutil.h"

#include <QVector>

using namespace Ps;
using PsTests::Reporter;

namespace {

/** 改动前的实现（逐字保留，勿改）。 */
QRgb oldCompositePixelOnto(QRgb backdrop, QRgb src, qreal opacity, BlendMode mode,
                           bool dissolve, int docX, int docY, int maskValue)
{
    int sr = 0, sg = 0, sb = 0, sa = 0;
    Premul::unpremultiplyRgb(src, &sr, &sg, &sb, &sa);
    if (sa == 0)
        return backdrop;
    if (maskValue >= 0) {
        sa = (sa * maskValue + 127) / 255;
        if (sa == 0)
            return backdrop;
    }
    int dr = 0, dg = 0, db = 0, da = 0;
    Premul::unpremultiplyRgb(backdrop, &dr, &dg, &db, &da);
    float la = (sa / 255.0f) * float(opacity);
    if (dissolve) {
        if (!Blend::dissolveKeeps(docX, docY, sa, opacity))
            return backdrop;
        la = 1.0f;
    }
    if (la <= 0.0f)
        return backdrop;
    const float cs[3] = { sr / 255.0f, sg / 255.0f, sb / 255.0f };
    const float cb[3] = { dr / 255.0f, dg / 255.0f, db / 255.0f };
    float out[3];
    const float ia = da / 255.0f;
    const float ar01 = la + (1.0f - la) * ia;
    if (ia <= 0.0f) {
        out[0] = cs[0];
        out[1] = cs[1];
        out[2] = cs[2];
    } else {
        const int back[3] = { dr, dg, db };
        const int srce[3] = { sr, sg, sb };
        float comp[3];
        Blend::pixel(mode, back, srce, comp);
        const float ratio = la / ar01;
        for (int c = 0; c < 3; ++c)
            out[c] = ratio * (ia * (comp[c] / 255.0f - cs[c]) + cs[c] - cb[c]) + cb[c];
    }
    const int ar = qBound(0, int(ar01 * 255.0f + 0.5f), 255);
    if (ar <= 0)
        return 0;
    return qRgba(qBound(0, int(out[0] * ar + 0.5f), 255),
                 qBound(0, int(out[1] * ar + 0.5f), 255),
                 qBound(0, int(out[2] * ar + 0.5f), 255), ar);
}

} // namespace

int tPixelPrim()
{
    Reporter r;

    // 前提校验：Normal 路径 comp = (n/255)*255，需 comp/255 == n/255 才可化简
    {
        int badMul = 0, badDiv = 0;
        for (int n = 0; n <= 255; ++n) {
            const float comp = (float(n) / 255.0f) * 255.0f;
            if (comp != float(n))
                ++badMul;
            if (comp / 255.0f != float(n) / 255.0f)
                ++badDiv;
        }
        r.check(badMul == 0 && badDiv == 0,
                QStringLiteral("化简前提：(n/255)*255==n 且 comp/255==cs 对 n∈[0,255] 全部成立"));
    }

    QVector<QRgb> pix;
    const int alphas[] = { 0, 1, 64, 128, 200, 254, 255 };
    const int vals[] = { 0, 1, 64, 127, 128, 200, 254, 255 };
    for (int a : alphas)
        for (int v : vals)
            pix.append(qRgba(v * a / 255, ((v * 7) % 256) * a / 255,
                             ((v * 13) % 256) * a / 255, a));

    const qreal ops[] = { 0.25, 0.5, 1.0 };
    const int masks[] = { -1, 0, 128, 255 };

    long long bad = 0, total = 0;
    for (int mi = 0; mi < kBlendModeCount; ++mi) {
        const BlendMode mode = blendModeFromIndex(mi);
        const bool dissolve = (mode == BlendMode::Dissolve);
        for (QRgb d : pix) {
            for (QRgb s : pix) {
                for (qreal op : ops) {
                    for (int mask : masks) {
                        for (int xy = 0; xy < 2; ++xy) {
                            const int x = xy ? 37 : 3;
                            const int y = xy ? 11 : 5;
                            const QRgb a = oldCompositePixelOnto(d, s, op, mode, dissolve, x, y, mask);
                            const QRgb b = compositePixelOnto(d, s, op, mode, dissolve, x, y, mask);
                            ++total;
                            if (a != b)
                                ++bad;
                        }
                    }
                }
            }
        }
    }
    r.info(QStringLiteral("穷举 %1 组，不一致 %2 组").arg(total).arg(bad));
    r.check(bad == 0, QStringLiteral("新实现与旧实现逐位一致"));
    return r.fails;
}
