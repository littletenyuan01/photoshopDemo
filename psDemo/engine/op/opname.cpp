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
    {OpName::LayerMode,            "ps:layer-mode",            "Layer Mode"},
    {OpName::BrightnessContrast,   "ps:brightness-contrast",   "Brightness/Contrast"},
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

} // namespace Ps
