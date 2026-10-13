/**
 * rastercost.cpp — OpName → OpCostNsPx（gpu-plan.md §4.3）。
 */
#include "rastercost.h"

#include "domain/filterstack.h"

namespace Ps {

OpCostNsPx opCostNsPerPixel(OpName name)
{
    switch (name) {
    case OpName::MedianBlur:
        return OpCostNsPx::MEDIAN_MEASURED;
    case OpName::Maximum:
    case OpName::Minimum:
    case OpName::DustAndScratches:
        return OpCostNsPx::EXTREMUM_FAMILY_EST;
    case OpName::ReduceNoise:
        return OpCostNsPx::REDUCE_NOISE_EST;
    case OpName::AddNoise:
        return OpCostNsPx::ADD_NOISE_MEASURED;
    case OpName::GaussianBlur:
        return OpCostNsPx::GAUSSIAN_MEASURED;
    case OpName::CustomFilter:
        return OpCostNsPx::CUSTOM_MEASURED;
    case OpName::UnsharpMask:
    case OpName::HighPass:
    case OpName::MotionBlur:
        return OpCostNsPx::BLUR_PLUS_MERGE_EST;
    case OpName::SharpenEdges:
        return OpCostNsPx::SHARPEN_EDGES_EST;
    case OpName::BoxBlur:
    case OpName::FindEdges:
    case OpName::Sharpen:
    case OpName::Emboss:
        return OpCostNsPx::LIGHT_3X3;
    case OpName::Ripple:
    case OpName::Offset:
        return OpCostNsPx::LIGHT_REMAP;
    case OpName::Mosaic:
    case OpName::Tiles:
        return OpCostNsPx::BLOCK_SCAN_EST;
    case OpName::Fragment:
        return OpCostNsPx::FRAGMENT_EST;
    case OpName::Wind:
        return OpCostNsPx::WIND_EST;
    case OpName::TraceContour:
        return OpCostNsPx::TRACE_CONTOUR_EST;
    case OpName::RadialBlur:
        return OpCostNsPx::RADIAL_BLUR_EST;
    case OpName::SurfaceBlur:
    case OpName::SmartBlur:
        return OpCostNsPx::EDGE_PRESERVING_EST;
    case OpName::BrightnessContrast:
    case OpName::Levels:
    case OpName::Curves:
    case OpName::Exposure:
    case OpName::Vibrance:
    case OpName::HueSaturation:
    case OpName::ColorBalance:
    case OpName::BlackAndWhite:
    case OpName::PhotoFilter:
    case OpName::ChannelMixer:
    case OpName::ColorLookup:
    case OpName::Invert:
    case OpName::Posterize:
    case OpName::Threshold:
    case OpName::GradientMap:
    case OpName::SelectiveColor:
        return OpCostNsPx::POINT_ADJUST;
    case OpName::Solarize:
        return OpCostNsPx::NEGLIGIBLE; // 表内 0.5 → 整型 0；偏向不上 GPU
    case OpName::StampDab:
    case OpName::FloodFill:
    case OpName::Gradient:
    case OpName::SolidFill:
    case OpName::SelectPolygon:
    case OpName::SelectFlood:
    case OpName::CloneStampDab:
    case OpName::FocusDab:
    case OpName::ToneDab:
    case OpName::ShapeFill:
    case OpName::FreeTransform:
    case OpName::LayerMode:
        return OpCostNsPx::NEGLIGIBLE;
    case OpName::Count:
        break;
    }
    return OpCostNsPx::UNREGISTERED;
}

int stackCostNsPerPixel(const FilterStack &stack)
{
    int sum = 0;
    for (int i = 0; i < stack.count(); ++i) {
        const FilterNode &n = stack.at(i);
        if (!n.isEnabled())
            continue;
        // 【点算子不计入链成本 —— 让这张表如实反映"GPU 能省下多少 CPU 工作量"】
        // 点算子在 CPU/GPU 两条路径上**都是 CPU 就地求值**（GPU 侧见 `runStack` 调
        // `FilterEval::applyNode`，T5.2），所以它们一个像素的 GPU 侧开销都省不掉；
        // 把它们累加进链成本，等于把一个"不上卡也白算"的量当成"上卡的理由"。
        //
        // ⚠️ **这不会改变当前的实际行为**：成本阈值只在 `Auto` 模式下才会被问到，
        // 而 `Auto` 是"永不调用 GPU"（§7.2.2 方案 A）；`PSLITE_RASTER=gpu` 走的是
        // `ForceGpu` 分支，那里**按设计跳过成本阈值**（只为门控/实测方便）。
        // 所以本条修的是**数据诚实性**（将来若开 Auto 的自动决策，$sum$ 必须是真值），
        // 不是一处性能 bug —— 曾在实测里把它误判成"点算子链会上卡变慢"，那是把
        // `ForceGpu` 的强制行为当成了成本判据的结果（见 gpu-plan §9 的实测纪律）。
        //
        // 判据与 `GpuRasterBackend` 的 CPU inline 穿透共用 `isPointOp()`（白名单只有一处）。
        if (isPointOp(n.op()))
            continue;
        const OpCostNsPx c = opCostNsPerPixel(n.op());
        if (c == OpCostNsPx::UNREGISTERED)
            return int(OpCostNsPx::UNREGISTERED);
        sum += int(c);
    }
    return sum;
}

bool isPointOp(OpName op)
{
    switch (op) {
    case OpName::LayerMode:
    case OpName::BrightnessContrast:
    case OpName::Levels:
    case OpName::Curves:
    case OpName::Exposure:
    case OpName::Vibrance:
    case OpName::HueSaturation:
    case OpName::ColorBalance:
    case OpName::BlackAndWhite:
    case OpName::PhotoFilter:
    case OpName::ChannelMixer:
    case OpName::ColorLookup:
    case OpName::Invert:
    case OpName::Posterize:
    case OpName::Threshold:
    case OpName::GradientMap:
    case OpName::SelectiveColor:
    case OpName::Solarize:
        return true;
    default:
        return false;
    }
}

} // namespace Ps
