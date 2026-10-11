/**
 * motionblurfilterop.h — 运动模糊（一维盒模糊沿 0°/45°/90°）。
 *
 * 对照 PS「滤镜→模糊→动感模糊」（距离 + 角度）。本实现是它的**子集**：
 * 角度只做 0°（水平）/ 45°（水平 + 垂直两趟，近似对角）/ 90°（垂直），
 * 不做任意角度；距离即盒模糊半径。
 *
 * 【一维盒模糊】每行/列用滑动窗口求和，O(1)/像素；abyss = 透明（层外即 0，与盒模糊同口径）。
 * 45° 就「先水平一趟再垂直一趟」—— 这不是严格对角，但观感近似，已在 docs 注明口径。
 *
 * 【直通 alpha】与盒模糊同口径：先 `qUnpremultiply`，模糊后 `qPremultiply`。
 */
#ifndef ENGINE_OP_MOTIONBLURFILTEROP_H
#define ENGINE_OP_MOTIONBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 运动模糊（一维盒模糊；0°/45°/90°）。 */
class MotionBlurFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::MotionBlur; }

    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_distance = 0;
    int m_angle = 0;
};

} // namespace Ps

#endif // ENGINE_OP_MOTIONBLURFILTEROP_H
