#ifndef ENGINE_BLEND_H
#define ENGINE_BLEND_H

#include "domain/blendmode.h"

#include <QtGlobal>

namespace Ps {

/**
 * 图层混合的**纯函数层**：只做「一个像素按某种模式混成什么颜色」，
 * 不管 Alpha 合成、不管图层遍历、不碰 Qt GUI（engine 分层要求）。
 *
 * 与 GIMP 的对应关系（一一对应，便于对照阅读）：
 * - 本文件 / `blend.cpp`            ⇔ `app/operations/layer-modes/gimpoperationlayermode-blend.c`
 * - `Compositor::blendTileOnto`    ⇔ `app/operations/layer-modes/gimpoperationlayermode-composite.c`
 *
 * 坐标约定与 GIMP 一致：`in` = **下方已合成结果**（backdrop），`layer` = **当前层**。
 */
namespace Blend {

/**
 * 【功能】逐通道混合：把 `layer` 按 `mode` 混到 `in` 上，结果写 `comp`。
 *
 * - 输入 `in`/`layer` 都是**直通（非预乘）**颜色，尺度 0..255；
 * - 输出 `comp` 同样是 0..255 尺度，**但不夹到 [0,255]** ——
 *   线性加深/减去/划分等会越界，GIMP 也是把越界值交给后面的合成公式，
 *   夹取只发生在最后写像素时（先夹会改变半透明下的结果，`docs/tech-notes.md` 有实测）。
 *
 * ⚠️ `BlendMode::Dissolve` 在这里等同 `Normal`：溶解的取舍是**逐像素随机阈值**，
 * 需要坐标，所以由 `Compositor` 调 `dissolveKeeps()` 决定，见下。
 */
void pixel(BlendMode mode, const int in[3], const int layer[3], float comp[3]);

/**
 * 【功能】溶解：这个像素是否保留上层。
 *
 * 对照 GIMP `gimpoperationdissolve.c`：`value = layer_alpha × opacity × 255`，
 * 随机数 `< value` 就取上层（并把它当**完全不透明**），否则原样保留下层。
 * 这正是「溶解 = 按不透明度做随机点状镂空」的观感来源。
 *
 * 【与 GIMP 的差异（实测/源码）】GIMP 用固定种子表 + `GRand`，本项目用
 * (x,y) 的整数哈希；两者都**只依赖坐标、可复现**，具体点阵不同。
 * （不用随机数发生器是必须的：本项目每次重绘都全量重合成，用真随机会让溶解层一直闪。）
 *
 * @param x,y        文档坐标（非层内坐标；GIMP 用的是 ROI 坐标）
 * @param layerAlpha 该像素的 Alpha（0..255）
 * @param opacity    图层不透明度（0..1）
 */
bool dissolveKeeps(int x, int y, int layerAlpha, qreal opacity);

} // namespace Blend
} // namespace Ps

#endif // ENGINE_BLEND_H
