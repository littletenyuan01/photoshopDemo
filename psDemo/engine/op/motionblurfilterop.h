/**
 * motionblurfilterop.h — 运动模糊（沿任意角度的一维盒模糊）。
 *
 * 对照 PS「滤镜→模糊→动感模糊」（距离 + 角度 0..179°）。
 *  · 0° / 90°：滑动窗口一维盒（O(1)/像素，与旧实现一致）；
 *  · 其它角度：沿方向最近邻取样平均（距离 = 半径）。
 *
 * 【直通 alpha】先 `qUnpremultiply`，模糊后 `qPremultiply`。
 * abyss = 透明（越界样本 0，仍除以完整窗口 2r+1）。
 */
#ifndef ENGINE_OP_MOTIONBLURFILTEROP_H
#define ENGINE_OP_MOTIONBLURFILTEROP_H

#include "filterop.h"

namespace Ps {

/** 运动模糊（一维盒模糊；角度 0..179°）。 */
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
