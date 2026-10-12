/**
 * rastercost.cpp — OpName → ns/px 表（gpu-plan.md §4.3）。
 */
#include "rastercost.h"

#include "domain/filterstack.h"

namespace Ps {

int opCostNsPerPixel(OpName name)
{
    switch (name) {
    case OpName::MedianBlur:
        return 187;
    case OpName::Maximum:
    case OpName::Minimum:
    case OpName::DustAndScratches:
        return 100;
    case OpName::ReduceNoise:
        return 50;
    case OpName::AddNoise:
        return 22;
    case OpName::GaussianBlur:
    case OpName::CustomFilter:
        return 20;
    case OpName::UnsharpMask:
    case OpName::HighPass:
    case OpName::MotionBlur:
        return 15;
    case OpName::SharpenEdges:
        return 10;
    case OpName::BoxBlur:
    case OpName::FindEdges:
    case OpName::Sharpen:
    case OpName::Emboss:
        return 5;
    case OpName::Ripple:
        return 2;
    case OpName::Offset:
        return 2; // 表内 1.5，整型取 2
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
        return 1;
    case OpName::Solarize:
        return 0; // 表内 0.5，整型取 0（点算子，偏向不上 GPU）
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
        return 0;
    case OpName::Count:
        break;
    }
    return -1;
}

int stackCostNsPerPixel(const FilterStack &stack)
{
    int sum = 0;
    for (int i = 0; i < stack.count(); ++i) {
        const FilterNode &n = stack.at(i);
        if (!n.isEnabled())
            continue;
        const int c = opCostNsPerPixel(n.op());
        if (c < 0)
            return -1;
        sum += c;
    }
    return sum;
}

} // namespace Ps
