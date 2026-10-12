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
        return 19; // 实测 ~18–21 ns/px @ r=3（§7.4 / T2.0）
    case OpName::CustomFilter:
        return 48; // 实测 ~46–53 ns/px（§7.4.1 / T2.7 标准测法）
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
    case OpName::Mosaic:
        return 8; // 估值：按块扫一遍均值
    case OpName::Fragment:
        return 6; // 估值：每像素 4 次取样
    case OpName::Wind:
        return 10; // 估值：每像素上风向窗口扫
    case OpName::TraceContour:
        return 4; // 估值：4-邻接跨阈值
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
