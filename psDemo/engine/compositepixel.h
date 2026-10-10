/**
 * compositepixel.h — 单像素合成（engine 层）。
 *
 * 把「解预乘 → 蒙版 → 混合色 → composite_union → 写回预乘」抽成单一函数，
 * 供 Compositor::blendTileOnto（现有合成路径）与 engine/graph BlendOp（图合成路径）
 * 共用，保证两条路径逐像素一致。
 * 对照 GIMP gimpoperationlayermode-composite.c 的 composite_union。
 */
#ifndef ENGINE_COMPOSITEPIXEL_H
#define ENGINE_COMPOSITEPIXEL_H

#include "blend.h"
#include "premul.h"

#include <QColor>
#include <QtGlobal>

namespace Ps {

/**
 * 把预乘源像素 @p src 按 @p mode / @p opacity 合成到预乘背景 @p backdrop 上。
 *
 * @param dissolve true 时按 Dissolve 取舍（需 @p docX/@p docY 复现点阵）
 * @param maskValue <0 无蒙版；否则 [0,255] 乘到源 alpha（白=255 显示）
 * @return 合成后的预乘背景像素
 */
inline QRgb compositePixelOnto(QRgb backdrop, QRgb src, qreal opacity, BlendMode mode,
                               bool dissolve, int docX, int docY, int maskValue = -1)
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
        return 0; // 全透明（预乘 0）

    const int pr = qBound(0, int(out[0] * ar + 0.5f), 255);
    const int pg = qBound(0, int(out[1] * ar + 0.5f), 255);
    const int pb = qBound(0, int(out[2] * ar + 0.5f), 255);
    return qRgba(pr, pg, pb, ar);
}

} // namespace Ps

#endif // ENGINE_COMPOSITEPIXEL_H
