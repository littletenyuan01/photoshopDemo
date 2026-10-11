/**
 * opsinit.cpp — opsinit.h 实现（engine/op 层）。
 *
 * 向 OpRegistry / PointOpRegistry 注册缓冲算子 + LayerModeOp；只存工厂，不构造实例。
 */
#include "opsinit.h"
#include "boxblurfilterop.h"
#include "clonestampdabop.h"
#include "customfilterop.h"
#include "dustscratchfilterop.h"
#include "embossfilterop.h"
#include "extremumfilterop.h"
#include "filterop.h"
#include "findedgesfilterop.h"
#include "floodfillop.h"
#include "gaussianblurfilterop.h"
#include "highpassfilterop.h"
#include "focusdabop.h"
#include "gradientop.h"
#include "layermodeop.h"
#include "medianblurfilterop.h"
#include "motionblurfilterop.h"
#include "offsetfilterop.h"
#include "opname.h"
#include "oppad.h"
#include "opregistry.h"
#include "pointopregistry.h"
#include "selectfloodop.h"
#include "sharpenedgesfilterop.h"
#include "sharpenfilterop.h"
#include "selectpolygonop.h"
#include "shapefillop.h"
#include "freetransformop.h"
#include "solidfillop.h"
#include "stampdabop.h"
#include "tonedabop.h"
#include "unsharpmaskfilterop.h"

namespace Ps {

void opsInit()
{
    // 绘制算子：Selection 裁剪由算子内自行判断 clip，不写入 requiredPads
    OpRegistry::add({
        OpName::StampDab,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new StampDabOp); },
    });

    OpRegistry::add({
        OpName::FloodFill,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new FloodFillOp); },
    });

    OpRegistry::add({
        OpName::Gradient,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new GradientOp); },
    });

    OpRegistry::add({
        OpName::SolidFill,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new SolidFillOp); },
    });

    // 选区写入：requiredPads = Selection
    OpRegistry::add({
        OpName::SelectPolygon,
        {OpPad::Selection},
        [] { return std::unique_ptr<BufferOp>(new SelectPolygonOp); },
    });

    OpRegistry::add({
        OpName::SelectFlood,
        {OpPad::Selection},
        [] { return std::unique_ptr<BufferOp>(new SelectFloodOp); },
    });

    OpRegistry::add({
        OpName::CloneStampDab,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new CloneStampDabOp); },
    });

    OpRegistry::add({
        OpName::FocusDab,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new FocusDabOp); },
    });

    OpRegistry::add({
        OpName::ToneDab,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new ToneDabOp); },
    });

    OpRegistry::add({
        OpName::ShapeFill,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new ShapeFillOp); },
    });

    OpRegistry::add({
        OpName::FreeTransform,
        {OpPad::Tiles},
        [] { return std::unique_ptr<BufferOp>(new FreeTransformOp); },
    });

    PointOpRegistry::add({
        OpName::LayerMode,
        [] { return std::unique_ptr<PointOp>(new LayerModeOp); },
    });

    // 滤镜算子（有临时资源，走 FilterOp 三段式；每线程一份实例）。
    // 点滤镜不在此注册 —— 它们无资源，由 FilterEval 直接分发，避免三段式开销。
    registerFilterOp(OpName::BoxBlur,
                     [] { return std::unique_ptr<FilterOp>(new BoxBlurFilterOp); });
    registerFilterOp(OpName::UnsharpMask,
                     [] { return std::unique_ptr<FilterOp>(new UnsharpMaskFilterOp); });
    registerFilterOp(OpName::FindEdges,
                     [] { return std::unique_ptr<FilterOp>(new FindEdgesFilterOp); });
    registerFilterOp(OpName::GaussianBlur,
                     [] { return std::unique_ptr<FilterOp>(new GaussianBlurFilterOp); });
    registerFilterOp(OpName::MedianBlur,
                     [] { return std::unique_ptr<FilterOp>(new MedianBlurFilterOp); });
    registerFilterOp(OpName::Sharpen,
                     [] { return std::unique_ptr<FilterOp>(new SharpenFilterOp); });
    registerFilterOp(OpName::Emboss,
                     [] { return std::unique_ptr<FilterOp>(new EmbossFilterOp); });
    // 几何算子（非面积算子：inputPad()==0，但只能整幅求值）
    registerFilterOp(OpName::Offset,
                     [] { return std::unique_ptr<FilterOp>(new OffsetFilterOp); });
    // 排序统计滤波（与中值同族）：最大 = 100% 分位、最小 = 0% 分位
    registerFilterOp(OpName::Maximum, [] {
        return std::unique_ptr<FilterOp>(
            new ExtremumFilterOp(ExtremumFilterOp::Mode::Maximum));
    });
    registerFilterOp(OpName::Minimum, [] {
        return std::unique_ptr<FilterOp>(
            new ExtremumFilterOp(ExtremumFilterOp::Mode::Minimum));
    });
    // 自定滤镜（3×3 卷积核）：验证「算子参数带数组」的路径
    registerFilterOp(OpName::CustomFilter,
                     [] { return std::unique_ptr<FilterOp>(new CustomFilterOp); });
    // 高通保留（复用盒模糊做模糊级）+ 运动模糊（一维盒模糊）
    registerFilterOp(OpName::HighPass,
                     [] { return std::unique_ptr<FilterOp>(new HighPassFilterOp); });
    registerFilterOp(OpName::MotionBlur,
                     [] { return std::unique_ptr<FilterOp>(new MotionBlurFilterOp); });
    registerFilterOp(OpName::DustAndScratches,
                     [] { return std::unique_ptr<FilterOp>(new DustAndScratchesFilterOp); });
    registerFilterOp(OpName::ReduceNoise,
                     [] { return std::unique_ptr<FilterOp>(new ReduceNoiseFilterOp); });
    registerFilterOp(OpName::SharpenEdges,
                     [] { return std::unique_ptr<FilterOp>(new SharpenEdgesFilterOp); });
}

} // namespace Ps
