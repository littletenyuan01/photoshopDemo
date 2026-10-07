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
    return name >= OpName::BrightnessContrast && name < OpName::Count;
}

} // namespace Ps
