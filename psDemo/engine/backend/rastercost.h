/**
 * rastercost.h — 每算子每像素成本（纳秒）与 GPU 决策阈值（gpu-plan.md §4）。
 *
 * 成本单位一律是 **纳秒/像素（ns/px）**；取值见 `OpCostNsPx`（对照 §4.3 表）。
 */
#ifndef ENGINE_BACKEND_RASTERCOST_H
#define ENGINE_BACKEND_RASTERCOST_H

#include "engine/op/opname.h"

namespace Ps {

/**
 * 单算子 CPU 成本档位（底层 int = ns/px）。
 *
 * - 名字后缀 `_MEASURED` = 本机实测回填；`_EST` = 估值（待测再回填）。
 * - `opCostNsPerPixel` 的返回值；未覆盖的 OpName 必须是 `UNREGISTERED`（覆盖性用例会红）。
 */
enum class OpCostNsPx : int {
    UNREGISTERED = -1, ///< 表未登记 —— 禁止静默当成 0

    NEGLIGIBLE = 0, ///< 绘制/选区/合成（不走滤镜链）；Solarize 表内 0.5 → 整型 0
    POINT_ADJUST = 1, ///< 亮度/色阶/曲线等点调整（估值 0.5–2）
    LIGHT_REMAP = 2, ///< Ripple 估值；Offset 表内 1.5 → 整型 2（memcpy 级）
    TRACE_CONTOUR_EST = 4, ///< 等高线：4-邻接跨电平
    LIGHT_3X3 = 5, ///< BoxBlur 实测临界线；FindEdges/Sharpen/Emboss 3×3 估值
    FRAGMENT_EST = 6, ///< 碎片：每像素 4 次取样
    BLOCK_SCAN_EST = 8, ///< Mosaic / Tiles：按块扫一遍
    SHARPEN_EDGES_EST = 10, ///< Sobel + 锐化
    WIND_EST = 10, ///< 上风向窗口扫
    BLUR_PLUS_MERGE_EST = 15, ///< USM / HighPass / MotionBlur（盒模糊 + 合并）
    GAUSSIAN_MEASURED = 19, ///< T2.0：~18–21 @ r=3
    ADD_NOISE_MEASURED = 22, ///< 1600×1200 高斯彩噪实测
    EDGE_PRESERVING_EST = 25, ///< SurfaceBlur / SmartBlur 保边邻域
    RADIAL_BLUR_EST = 40, ///< 每像素多点旋转/缩放取样
    CUSTOM_MEASURED = 48, ///< T2.7：~46–53
    REDUCE_NOISE_EST = 50, ///< 中值 + 混合
    EXTREMUM_FAMILY_EST = 100, ///< Maximum / Minimum / DustAndScratches
    MEDIAN_MEASURED = 187, ///< 512² r=3 ≈ 49 ms
};

/** GPU 决策阈值（各自单位写在枚举名里）。 */
enum class GpuDecisionThreshold : int {
    /**
     * 链成本（ns/px）须**严格大于**此值才考虑 GPU。
     * 取 `LIGHT_3X3`(=5)：单独 BoxBlur 不上卡。
     */
    CHAIN_COST_NS_PER_PIXEL = int(OpCostNsPx::LIGHT_3X3),
    /** 含 pad 后整块缓冲的像素数下限（100k）。 */
    MIN_PIXELS = 100 * 1000,
};

/** 兼容旧调用点：链成本阈值（ns/px）。 */
constexpr int kGpuChainCostThresholdNsPerPixel =
    int(GpuDecisionThreshold::CHAIN_COST_NS_PER_PIXEL);

/** 兼容旧调用点：像素数下限。 */
constexpr int kMinPixelsForGpu = int(GpuDecisionThreshold::MIN_PIXELS);

/** 每算子 ns/px；未登记返回 `OpCostNsPx::UNREGISTERED`。 */
OpCostNsPx opCostNsPerPixel(OpName name);

/**
 * 是否为「点算子」（逐像素、无邻域、无临时资源）。
 *
 * **唯一白名单，两个用途共用**（口径只有一处，避免两处各写一份而漂）：
 *  1. `stackCostNsPerPixel` **跳过**它们 —— 点算子在 CPU/GPU 两条路径上都是 CPU 就地求值
 *     （GPU 侧见 `runStack` 调 `FilterEval::applyNode`），算进链成本只会白抬高、
 *     把不该上卡的链推过阈值；
 *  2. `GpuRasterBackend` 用它判断「该算子能不能走 CPU inline 穿透」。
 *
 * 加新的调整类算子时**只改这一处**。
 */
bool isPointOp(OpName name);

/**
 * 栈内启用节点的成本之和（ns/px）。
 * 遇未登记项返回 `int(OpCostNsPx::UNREGISTERED)`（= -1）。
 */
int stackCostNsPerPixel(const class FilterStack &stack);

} // namespace Ps

#endif
