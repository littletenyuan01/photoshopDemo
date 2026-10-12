/**
 * rastercost.h — 每算子每像素成本（纳秒）与 GPU 决策阈值（gpu-plan.md §4）。
 */
#ifndef ENGINE_BACKEND_RASTERCOST_H
#define ENGINE_BACKEND_RASTERCOST_H

#include "engine/op/opname.h"

namespace Ps {

/** 每算子 ns/px；未登记返回 -1（用例会红）。 */
int opCostNsPerPixel(OpName name);

/** 链成本严格大于此值才考虑 GPU（单独 BoxBlur=5 不上）。 */
constexpr int kGpuChainCostThresholdNsPerPixel = 5;

/** 像素数下限（含 pad 后的整块缓冲）。 */
constexpr int kMinPixelsForGpu = 100 * 1000;

/** 栈内所有启用节点的成本之和；遇未登记项返回 -1。 */
int stackCostNsPerPixel(const class FilterStack &stack);

} // namespace Ps

#endif
