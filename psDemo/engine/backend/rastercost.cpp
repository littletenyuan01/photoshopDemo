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
        const OpCostNsPx c = opCostNsPerPixel(n.op());
        if (c == OpCostNsPx::UNREGISTERED)
            return int(OpCostNsPx::UNREGISTERED);
        sum += int(c);
    }
    return sum;
}

} // namespace Ps
