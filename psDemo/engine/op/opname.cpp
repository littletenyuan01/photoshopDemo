/**
 * opname.cpp — opname.h 实现（engine/op 层）。
 *
 * kOpNameTable 为唯一名字表；对照 gimp-layer-modes.c 的 GimpLayerModeInfo。
 */
#include "opname.h"

namespace Ps {

namespace {

struct OpNameInfo {
    OpName name;
    const char *id;
    const char *title;
};

/** 唯一名字表（对照 gimp-layer-modes.c 的 GimpLayerModeInfo 表）。 */
constexpr OpNameInfo kOpNameTable[] = {
    {OpName::StampDab,             "ps:stamp-dab",             "Stamp Dab"},
    {OpName::FloodFill,            "ps:flood-fill",            "Flood Fill"},
    {OpName::Gradient,             "ps:gradient",              "Gradient"},
    {OpName::SolidFill,            "ps:solid-fill",            "Solid Fill"},
    {OpName::SelectPolygon,        "ps:select-polygon",        "Select Polygon"},
    {OpName::SelectFlood,          "ps:select-flood",          "Select Flood"},
    {OpName::CloneStampDab,        "ps:clone-stamp-dab",       "Clone Stamp Dab"},
    {OpName::FocusDab,             "ps:focus-dab",             "Focus Dab"},
    {OpName::ToneDab,              "ps:tone-dab",              "Tone Dab"},
    {OpName::ShapeFill,            "ps:shape-fill",            "Shape Fill"},
    {OpName::FreeTransform,        "ps:free-transform",        "Free Transform"},
    {OpName::LayerMode,            "ps:layer-mode",            "Layer Mode"},
    {OpName::BrightnessContrast,   "ps:brightness-contrast",   "亮度/对比度"},
    {OpName::Levels,               "ps:levels",                "色阶"},
    {OpName::Curves,               "ps:curves",                "曲线"},
    {OpName::Exposure,             "ps:exposure",              "曝光度"},
    {OpName::Vibrance,             "ps:vibrance",              "自然饱和度"},
    {OpName::HueSaturation,        "ps:hue-saturation",        "色相/饱和度"},
    {OpName::ColorBalance,         "ps:color-balance",         "色彩平衡"},
    {OpName::BlackAndWhite,        "ps:black-and-white",       "黑白"},
    {OpName::PhotoFilter,          "ps:photo-filter",          "照片滤镜"},
    {OpName::ChannelMixer,         "ps:channel-mixer",         "通道混合器"},
    {OpName::ColorLookup,          "ps:color-lookup",          "颜色查找"},
    {OpName::Invert,               "ps:invert",                "反相"},
    {OpName::Posterize,            "ps:posterize",             "色调分离"},
    {OpName::Threshold,            "ps:threshold",             "阈值"},
    {OpName::GradientMap,          "ps:gradient-map",          "渐变映射"},
    {OpName::SelectiveColor,       "ps:selective-color",       "可选颜色"},
    {OpName::BoxBlur,              "ps:box-blur",              "盒模糊"},
    {OpName::UnsharpMask,          "ps:unsharp-mask",          "USM 锐化"},
    {OpName::FindEdges,            "ps:find-edges",            "查找边缘"},
    {OpName::GaussianBlur,         "ps:gaussian-blur",         "高斯模糊"},
    {OpName::MedianBlur,           "ps:median-blur",           "中间值"},
    {OpName::Sharpen,              "ps:sharpen",               "锐化"},
    {OpName::Emboss,               "ps:emboss",                "浮雕效果"},
    {OpName::Offset,               "ps:offset",                "位移"},
    {OpName::Maximum,              "ps:maximum",               "最大值"},
    {OpName::Minimum,              "ps:minimum",               "最小值"},
    {OpName::CustomFilter,         "ps:custom-filter",         "自定"},
    {OpName::HighPass,             "ps:high-pass",             "高通保留"},
    {OpName::MotionBlur,           "ps:motion-blur",           "运动模糊"},
    {OpName::AddNoise,             "ps:add-noise",             "添加杂色"},
    {OpName::Solarize,             "ps:solarize",              "曝光过度"},
    {OpName::DustAndScratches,     "ps:dust-and-scratches",    "蒙尘与划痕"},
    {OpName::ReduceNoise,          "ps:reduce-noise",          "减少杂色"},
    {OpName::SharpenEdges,         "ps:sharpen-edges",         "锐化边缘"},
    {OpName::Ripple,               "ps:ripple",                "波纹"},
    {OpName::Mosaic,               "ps:mosaic",                "马赛克"},
    {OpName::Fragment,             "ps:fragment",              "碎片"},
    {OpName::Wind,                 "ps:wind",                  "风"},
    {OpName::TraceContour,         "ps:trace-contour",         "等高线"},
    {OpName::RadialBlur,           "ps:radial-blur",           "径向模糊"},
    {OpName::SurfaceBlur,          "ps:surface-blur",          "表面模糊"},
    {OpName::SmartBlur,            "ps:smart-blur",            "特殊模糊"},
    {OpName::Tiles,                "ps:tiles",                 "拼贴"},
};

const OpNameInfo *infoFor(OpName name)
{
    for (const OpNameInfo &row : kOpNameTable) {
        if (row.name == name)
            return &row;
    }
    return nullptr;
}

} // namespace

QString opNameId(OpName name)
{
    if (const OpNameInfo *info = infoFor(name))
        return QString::fromUtf8(info->id);
    return {};
}

QString opNameTitle(OpName name)
{
    if (const OpNameInfo *info = infoFor(name))
        return QString::fromUtf8(info->title);
    return {};
}

bool isAdjustmentOp(OpName name)
{
    // 面积算子已可作调整层：合成器对 pad>0 走「下方合成 → 子缓冲外扩 → 只写回请求区」
    // （见 compositor.cpp blendLayerRangeImpl）。
    // 仍排除：① 几何 Offset（滤的是层自身像素语义，不是下方合成）；
    // ② 位置相关（噪声/马赛克/径向/拼贴/波纹）—— 依赖绝对坐标，分块下方合成会对不齐。
    switch (name) {
    case OpName::Offset:
    case OpName::AddNoise:
    case OpName::Ripple:
    case OpName::Mosaic:
    case OpName::RadialBlur:
    case OpName::Tiles:
        return false;
    default:
        break;
    }
    return name >= OpName::BrightnessContrast && name < OpName::Count;
}

} // namespace Ps
