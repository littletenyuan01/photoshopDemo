/**
 * filternode.h — 图层滤镜节点：OpName + 开关 + 参数（domain 层）。
 *
 * 非破坏；求值在 FilterStack::apply，不写回 Layer 瓦片。
 */
#ifndef DOMAIN_FILTERNODE_H
#define DOMAIN_FILTERNODE_H

#include "engine/op/opname.h"

#include <QRect>
#include <QString>
#include <QtGlobal>

#include <cmath>

namespace Ps {

/**
 * 图层滤镜节点（对照 GimpDrawableFilter：可开关、带参数，不写回层像素）。
 * 参数字段按 OpName 解释；未用字段保持默认即可。
 */
class FilterNode
{
public:
    FilterNode() = default;
    explicit FilterNode(OpName op)
        : m_op(op)
    {
    }

    OpName op() const { return m_op; }
    void setOp(OpName op) { m_op = op; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    /** 面板显示名（来自 OpName 名字表）。 */
    QString title() const { return opNameTitle(m_op); }

    // —— 亮度/对比度 ——
    qreal brightness() const { return m_brightness; }
    void setBrightness(qreal v) { m_brightness = qBound(-1.0, v, 1.0); }
    qreal contrast() const { return m_contrast; }
    void setContrast(qreal v) { m_contrast = qBound(-1.0, v, 1.0); }

    // —— 色相/饱和度 / 自然饱和度 ——
    qreal hue() const { return m_hue; }
    void setHue(qreal v) { m_hue = qBound(-180.0, v, 180.0); }
    qreal saturation() const { return m_saturation; }
    void setSaturation(qreal v) { m_saturation = qBound(-100.0, v, 100.0); }
    qreal lightness() const { return m_lightness; }
    void setLightness(qreal v) { m_lightness = qBound(-100.0, v, 100.0); }
    qreal vibrance() const { return m_vibrance; }
    void setVibrance(qreal v) { m_vibrance = qBound(-100.0, v, 100.0); }

    // —— 曝光 ——
    qreal exposure() const { return m_exposure; }
    void setExposure(qreal v) { m_exposure = qBound(-20.0, v, 20.0); }
    qreal exposureOffset() const { return m_exposureOffset; }
    void setExposureOffset(qreal v) { m_exposureOffset = qBound(-0.5, v, 0.5); }
    qreal gammaCorrection() const { return m_gammaCorrection; }
    void setGammaCorrection(qreal v) { m_gammaCorrection = qBound(0.01, v, 9.99); }

    // —— 色阶（输入黑/白 0..255，灰度伽马）——
    qreal levelsBlack() const { return m_levelsBlack; }
    void setLevelsBlack(qreal v) { m_levelsBlack = qBound(0.0, v, 255.0); }
    qreal levelsWhite() const { return m_levelsWhite; }
    void setLevelsWhite(qreal v) { m_levelsWhite = qBound(0.0, v, 255.0); }
    qreal levelsGamma() const { return m_levelsGamma; }
    void setLevelsGamma(qreal v) { m_levelsGamma = qBound(0.1, v, 9.99); }

    // —— 色彩平衡（青-红 / 品红-绿 / 黄-蓝，-100..100）——
    qreal colorBalanceCR() const { return m_colorBalanceCR; }
    void setColorBalanceCR(qreal v) { m_colorBalanceCR = qBound(-100.0, v, 100.0); }
    qreal colorBalanceMG() const { return m_colorBalanceMG; }
    void setColorBalanceMG(qreal v) { m_colorBalanceMG = qBound(-100.0, v, 100.0); }
    qreal colorBalanceYB() const { return m_colorBalanceYB; }
    void setColorBalanceYB(qreal v) { m_colorBalanceYB = qBound(-100.0, v, 100.0); }

    // —— 照片滤镜 ——
    qreal photoFilterHue() const { return m_photoFilterHue; }
    void setPhotoFilterHue(qreal v) { m_photoFilterHue = qBound(0.0, v, 360.0); }
    qreal photoFilterDensity() const { return m_photoFilterDensity; }
    void setPhotoFilterDensity(qreal v) { m_photoFilterDensity = qBound(0.0, v, 100.0); }

    // —— 黑白各通道权重（简化为相对权重）——
    qreal bwReds() const { return m_bwReds; }
    void setBwReds(qreal v) { m_bwReds = qBound(-200.0, v, 300.0); }
    qreal bwYellows() const { return m_bwYellows; }
    void setBwYellows(qreal v) { m_bwYellows = qBound(-200.0, v, 300.0); }
    qreal bwGreens() const { return m_bwGreens; }
    void setBwGreens(qreal v) { m_bwGreens = qBound(-200.0, v, 300.0); }
    qreal bwCyans() const { return m_bwCyans; }
    void setBwCyans(qreal v) { m_bwCyans = qBound(-200.0, v, 300.0); }
    qreal bwBlues() const { return m_bwBlues; }
    void setBwBlues(qreal v) { m_bwBlues = qBound(-200.0, v, 300.0); }
    qreal bwMagentas() const { return m_bwMagentas; }
    void setBwMagentas(qreal v) { m_bwMagentas = qBound(-200.0, v, 300.0); }

    // —— 色调分离 / 阈值 ——
    int posterizeLevels() const { return m_posterizeLevels; }
    void setPosterizeLevels(int v) { m_posterizeLevels = qBound(2, v, 255); }
    int threshold() const { return m_threshold; }
    void setThreshold(int v) { m_threshold = qBound(0, v, 255); }

    // —— 曲线：主通道 5 点输出（输入固定 0/64/128/192/255）——
    int curveY0() const { return m_curveY[0]; }
    int curveY1() const { return m_curveY[1]; }
    int curveY2() const { return m_curveY[2]; }
    int curveY3() const { return m_curveY[3]; }
    int curveY4() const { return m_curveY[4]; }
    void setCurveY0(int v) { m_curveY[0] = qBound(0, v, 255); }
    void setCurveY1(int v) { m_curveY[1] = qBound(0, v, 255); }
    void setCurveY2(int v) { m_curveY[2] = qBound(0, v, 255); }
    void setCurveY3(int v) { m_curveY[3] = qBound(0, v, 255); }
    void setCurveY4(int v) { m_curveY[4] = qBound(0, v, 255); }
    const int *curveYs() const { return m_curveY; }

    // —— 通道混合器：输出行 × 输入列，百分数；单色 ——
    qreal mixRr() const { return m_mix[0]; }
    qreal mixRg() const { return m_mix[1]; }
    qreal mixRb() const { return m_mix[2]; }
    qreal mixGr() const { return m_mix[3]; }
    qreal mixGg() const { return m_mix[4]; }
    qreal mixGb() const { return m_mix[5]; }
    qreal mixBr() const { return m_mix[6]; }
    qreal mixBg() const { return m_mix[7]; }
    qreal mixBb() const { return m_mix[8]; }
    void setMixRr(qreal v) { m_mix[0] = qBound(-200.0, v, 200.0); }
    void setMixRg(qreal v) { m_mix[1] = qBound(-200.0, v, 200.0); }
    void setMixRb(qreal v) { m_mix[2] = qBound(-200.0, v, 200.0); }
    void setMixGr(qreal v) { m_mix[3] = qBound(-200.0, v, 200.0); }
    void setMixGg(qreal v) { m_mix[4] = qBound(-200.0, v, 200.0); }
    void setMixGb(qreal v) { m_mix[5] = qBound(-200.0, v, 200.0); }
    void setMixBr(qreal v) { m_mix[6] = qBound(-200.0, v, 200.0); }
    void setMixBg(qreal v) { m_mix[7] = qBound(-200.0, v, 200.0); }
    void setMixBb(qreal v) { m_mix[8] = qBound(-200.0, v, 200.0); }
    bool mixMonochrome() const { return m_mixMonochrome; }
    void setMixMonochrome(bool on) { m_mixMonochrome = on; }

    // —— 颜色查找：内置预设 0..3 ——
    int colorLookupPreset() const { return m_colorLookupPreset; }
    void setColorLookupPreset(int v) { m_colorLookupPreset = qBound(0, v, 3); }

    // —— 渐变映射：双色 + 强度 ——
    quint32 gradientMapColorA() const { return m_gradColorA; }
    quint32 gradientMapColorB() const { return m_gradColorB; }
    void setGradientMapColorA(quint32 rgba) { m_gradColorA = rgba; }
    void setGradientMapColorB(quint32 rgba) { m_gradColorB = rgba; }
    qreal gradientMapStrength() const { return m_gradStrength; }
    void setGradientMapStrength(qreal v) { m_gradStrength = qBound(0.0, v, 100.0); }

    // —— 可选颜色：目标色 0..8 + CMYK 偏移 ——
    int selectiveColorTarget() const { return m_selTarget; }
    void setSelectiveColorTarget(int v) { m_selTarget = qBound(0, v, 8); }
    qreal selectiveCyan() const { return m_selC; }
    qreal selectiveMagenta() const { return m_selM; }
    qreal selectiveYellow() const { return m_selY; }
    qreal selectiveBlack() const { return m_selK; }
    void setSelectiveCyan(qreal v) { m_selC = qBound(-100.0, v, 100.0); }
    void setSelectiveMagenta(qreal v) { m_selM = qBound(-100.0, v, 100.0); }
    void setSelectiveYellow(qreal v) { m_selY = qBound(-100.0, v, 100.0); }
    void setSelectiveBlack(qreal v) { m_selK = qBound(-100.0, v, 100.0); }

    // —— 盒模糊（面积算子）：半径（像素）——
    int blurRadius() const { return m_blurRadius; }
    void setBlurRadius(int v) { m_blurRadius = qBound(0, v, 128); }

    // —— USM 锐化（面积算子）：模糊半径 / 强度 / 阈值 ——
    int usmRadius() const { return m_usmRadius; }
    void setUsmRadius(int v) { m_usmRadius = qBound(1, v, 64); }
    qreal usmAmount() const { return m_usmAmount; }
    void setUsmAmount(qreal v) { m_usmAmount = qBound(0.0, v, 5.0); }
    int usmThreshold() const { return m_usmThreshold; }
    void setUsmThreshold(int v) { m_usmThreshold = qBound(0, v, 255); }

    // —— 高斯模糊（面积算子）：半径（UI 口径，映射见 gaussianblurfilterop.h）——
    int gaussRadius() const { return m_gaussRadius; }
    void setGaussRadius(int v) { m_gaussRadius = qBound(1, v, 64); }

    // —— 中值模糊（面积算子）：半径（1..8；滑窗直方图，成本与窗口面积无关）——
    int medianRadius() const { return m_medianRadius; }
    void setMedianRadius(int v) { m_medianRadius = qBound(1, v, 8); }

    // —— 锐化（面积算子）：3×3 线性核强度（1..4；1 = 经典锐化，更大 ≈ 进一步锐化）——
    int sharpenStrength() const { return m_sharpenStrength; }
    void setSharpenStrength(int v) { m_sharpenStrength = qBound(1, v, 4); }

    // —— 浮雕（面积算子）：3×3 方向性核强度（1..4）——
    int embossStrength() const { return m_embossStrength; }
    void setEmbossStrength(int v) { m_embossStrength = qBound(1, v, 4); }

    // —— 位移：水平/垂直像素偏移（正值 = 内容向右/向下移）；移出即丢、空出透明 ——
    int offsetDx() const { return m_offsetDx; }
    void setOffsetDx(int v) { m_offsetDx = qBound(-2048, v, 2048); }
    int offsetDy() const { return m_offsetDy; }
    void setOffsetDy(int v) { m_offsetDy = qBound(-2048, v, 2048); }

    // —— 最大值 / 最小值（排序统计滤波）：半径（1..8；滑窗直方图，与中值同构）——
    int extremumRadius() const { return m_extremumRadius; }
    void setExtremumRadius(int v) { m_extremumRadius = qBound(1, v, 8); }

    // —— 自定滤镜（3×3 卷积核）：9 个权重（-999..999）+ 缩放（0=不除）+ 位移 ——
    int customWeight(int i) const { return m_customW[qBound(0, i, 8)]; }
    void setCustomWeight(int i, int v) { m_customW[qBound(0, i, 8)] = qBound(-999, v, 999); }
    int customScale() const { return m_customScale; }
    void setCustomScale(int v) { m_customScale = qBound(0, v, 9999); }
    int customOffset() const { return m_customOffset; }
    void setCustomOffset(int v) { m_customOffset = qBound(-255, v, 255); }

    // —— 高通保留：半径（1..64；src - 盒模糊 + 128）——
    int highPassRadius() const { return m_highPassRadius; }
    void setHighPassRadius(int v) { m_highPassRadius = qBound(1, v, 64); }

    // —— 运动模糊：距离（1..128）+ 角度（0..179°，一维盒模糊方向）——
    int motionDistance() const { return m_motionDistance; }
    void setMotionDistance(int v) { m_motionDistance = qBound(1, v, 128); }
    int motionAngle() const { return m_motionAngle; }
    void setMotionAngle(int v) { m_motionAngle = ((v % 180) + 180) % 180; }

    // —— 波纹：振幅（0..64）+ 波长（1..256）；out(x,y)=in(x+A·sin(2πy/λ), y) ——
    int rippleAmplitude() const { return m_rippleAmplitude; }
    void setRippleAmplitude(int v) { m_rippleAmplitude = qBound(0, v, 64); }
    int rippleWavelength() const { return m_rippleWavelength; }
    void setRippleWavelength(int v) { m_rippleWavelength = qBound(1, v, 256); }

    // —— 马赛克：方块边长（2..64）；格子按层内绝对坐标对齐 ——
    int mosaicCell() const { return m_mosaicCell; }
    void setMosaicCell(int v) { m_mosaicCell = qBound(2, v, 64); }

    // —— 风：方向（0 左→右 / 1 右→左）+ 方法（0 风 / 1 爆发 / 2 交错）+ 强度（1..20）——
    int windDirection() const { return m_windDirection; }
    void setWindDirection(int v) { m_windDirection = qBound(0, v, 1); }
    int windMethod() const { return m_windMethod; }
    void setWindMethod(int v) { m_windMethod = qBound(0, v, 2); }
    int windStrength() const { return m_windStrength; }
    void setWindStrength(int v) { m_windStrength = qBound(1, v, 20); }

    // —— 等高线：电平（0..255）+ 边缘（0 较低 / 1 较高）——
    int contourLevel() const { return m_contourLevel; }
    void setContourLevel(int v) { m_contourLevel = qBound(0, v, 255); }
    int contourEdge() const { return m_contourEdge; }
    void setContourEdge(int v) { m_contourEdge = qBound(0, v, 1); }

    // —— 径向模糊：数量（1..50）+ 方法（0 旋转 / 1 缩放）；图心 → 整层 ——
    int radialAmount() const { return m_radialAmount; }
    void setRadialAmount(int v) { m_radialAmount = qBound(1, v, 50); }
    int radialMethod() const { return m_radialMethod; }
    void setRadialMethod(int v) { m_radialMethod = qBound(0, v, 1); }

    // —— 表面模糊：半径（1..20）+ 阈值（0..255）——
    int surfaceRadius() const { return m_surfaceRadius; }
    void setSurfaceRadius(int v) { m_surfaceRadius = qBound(1, v, 20); }
    int surfaceThreshold() const { return m_surfaceThreshold; }
    void setSurfaceThreshold(int v) { m_surfaceThreshold = qBound(0, v, 255); }

    // —— 特殊模糊：半径 + 阈值 + 模式（0 正常 / 1 仅边缘 / 2 叠加边缘）——
    int smartRadius() const { return m_smartRadius; }
    void setSmartRadius(int v) { m_smartRadius = qBound(1, v, 20); }
    int smartThreshold() const { return m_smartThreshold; }
    void setSmartThreshold(int v) { m_smartThreshold = qBound(0, v, 255); }
    int smartMode() const { return m_smartMode; }
    void setSmartMode(int v) { m_smartMode = qBound(0, v, 2); }

    // —— 拼贴：每边块数 + 最大偏移% + 空隙填充 + 种子（确定性）——
    int tilesCount() const { return m_tilesCount; }
    void setTilesCount(int v) { m_tilesCount = qBound(2, v, 100); }
    int tilesMaxOffset() const { return m_tilesMaxOffset; }
    void setTilesMaxOffset(int v) { m_tilesMaxOffset = qBound(0, v, 100); }
    /** 0 透明（背景）/ 1 反相 / 2 原图未改。 */
    int tilesFill() const { return m_tilesFill; }
    void setTilesFill(int v) { m_tilesFill = qBound(0, v, 2); }
    quint32 tilesSeed() const { return m_tilesSeed; }
    void setTilesSeed(quint32 v) { m_tilesSeed = v; }

    // —— 添加杂色：数量（0..100 %）+ 分布（均匀/高斯）+ 单色 + 种子 ——
    int noiseAmount() const { return m_noiseAmount; }
    void setNoiseAmount(int v) { m_noiseAmount = qBound(0, v, 100); }
    bool noiseGaussian() const { return m_noiseGaussian; }
    void setNoiseGaussian(bool on) { m_noiseGaussian = on; }
    bool noiseMonochromatic() const { return m_noiseMonochromatic; }
    void setNoiseMonochromatic(bool on) { m_noiseMonochromatic = on; }
    /**
     * 噪声种子。**必须持久化且不进「随机每次不同」的路径**：噪声被当作
     * `(x, y, seed, 通道)` 的确定函数（见 `engine/filtereval.cpp` 的 `applyAddNoise`），
     * 这样预览/定稿/撤销/重开文件都会得到**同一张**噪声图，不需要保存噪声位图。
     */
    quint32 noiseSeed() const { return m_noiseSeed; }
    void setNoiseSeed(quint32 v) { m_noiseSeed = v; }

    // —— 减少杂色（Demo：中值 + 与原图按强度混合；0..100%）——
    int reduceNoiseStrength() const { return m_reduceNoiseStrength; }
    void setReduceNoiseStrength(int v) { m_reduceNoiseStrength = qBound(0, v, 100); }

    /**
     * 「想算出 `output` 这块像素，输入至少要有哪一块」—— **层内坐标**（与 output 同一空间）。
     *
     * 这是 `inputPad()`（一个 int）的**泛化**：pad 只能表达**对称**外扩，而有些算子的需求
     * 是**不对称**的 —— 运动模糊 0° 只要左右邻居、上下一个都不用。用对称外扩会把上下也各扩
     * `distance`，distance=128、块 64 时等于多取 25 倍像素（这正是 ROI 通用化第 ② 步要治的浪费）。
     *
     * 【默认实现】对称外扩 `inputPad()` —— 与旧行为**逐像素一致**，所以七个面积算子的调用方
     * 观感不变（只做 ② 时全套测试必须保持绿，这是本步的验收标准）。
     *
     * 【位移】取「输出区 ∪ 平移后的输出区」—— 仍是**一个矩形**，所以也能逐块求值
     * （曾有一档「环绕」是多矩形依赖、只能整层求值，现已移除；见 offsetfilterop.cpp 顶部说明）。
     */
    QRect requiredInputs(const QRect &output) const
    {
        if (op() == OpName::MotionBlur) {
            const int d = m_motionDistance;
            const int ang = ((m_motionAngle % 180) + 180) % 180;
            if (ang == 0)
                return output.adjusted(-d, 0, d, 0);
            if (ang == 90)
                return output.adjusted(0, -d, 0, d);
            // 任意角：按方向分量外扩（ceil(|d·cos|), ceil(|d·sin|)）
            const double rad = double(ang) * 3.14159265358979323846 / 180.0;
            const int padX = int(std::ceil(std::abs(d * std::cos(rad)) - 1e-9));
            const int padY = int(std::ceil(std::abs(d * std::sin(rad)) - 1e-9));
            return output.adjusted(-padX, -padY, padX, padY);
        }
        if (op() == OpName::Ripple) {
            const int A = m_rippleAmplitude;
            return (A > 0) ? output.adjusted(-A, 0, A, 0) : output;
        }
        if (op() == OpName::Wind) {
            const int S = (m_windMethod == 1) ? (m_windStrength * 2) : m_windStrength;
            if (S <= 0)
                return output;
            if (m_windMethod == 2) // 交错：奇偶行方向相反，两侧都要
                return output.adjusted(-S, 0, S, 0);
            // 0=从左吹：上风在左；1=从右吹：上风在右
            return (m_windDirection == 0) ? output.adjusted(-S, 0, 0, 0)
                                         : output.adjusted(0, 0, S, 0);
        }
        if (op() == OpName::Offset) {
            // 取图窗口取「输出区 ∪ 平移后的输出区」——**一个矩形**，因此可以逐块求值。
            // 算子照旧做 (dx,dy) 平移，调用方再按 `want − srcWant` 裁回，两步合起来正好等于
            // 「从平移后的位置取像素」；越界部分由 extract 填透明 = 层外透明。
            return output.united(output.translated(-m_offsetDx, -m_offsetDy));
        }
        const int p = inputPad();
        return (p > 0) ? output.adjusted(-p, -p, p, p) : output;
    }

    /**
     * **预览降采样**用：返回一份「空间类参数按 @p divisor 缩小」的副本。
     *
     * 【为什么需要】降采样预览（PS 那种「更糊但更快」）是把输入图缩小 @c divisor 倍再求值。
     * 若半径保持原值，效果会**强 @c divisor 倍**（预览与定稿差得离谱、拖动时像另一个滤镜）。
     * 所以必须把「与像素距离有关」的参数一起缩：
     *   · 缩：盒模糊/高斯/USM/中值/最大值最小值/高通半径、运动模糊距离、位移 dx/dy；
     *   · 不缩：强度、阈值、角度、自定义核权重、缩放/位移偏置、各类颜色参数（与分辨率无关）。
     *
     * 半径一律 `max(1, ceil(v/divisor))`：0 会让算子退化成恒等，预览就什么都看不到；
     * 但原本就是 0 的（例如盒模糊半径 0 = 不处理）保持 0，不能凭空造出效果。
     */
    FilterNode scaledForDownsampledPreview(int divisor) const
    {
        if (divisor <= 1)
            return *this;
        FilterNode n = *this;
        const auto shrink = [divisor](int v) {
            return (v <= 0) ? 0 : qMax(1, (v + divisor - 1) / divisor);
        };
        n.m_blurRadius = shrink(m_blurRadius);
        n.m_usmRadius = shrink(m_usmRadius);
        n.m_gaussRadius = shrink(m_gaussRadius);
        n.m_medianRadius = shrink(m_medianRadius);
        n.m_extremumRadius = shrink(m_extremumRadius);
        n.m_highPassRadius = shrink(m_highPassRadius);
        n.m_motionDistance = shrink(m_motionDistance);
        n.m_offsetDx = m_offsetDx / divisor;
        n.m_offsetDy = m_offsetDy / divisor;
        n.m_mosaicCell = shrink(m_mosaicCell);
        if (n.m_mosaicCell < 2)
            n.m_mosaicCell = 2;
        n.m_windStrength = shrink(m_windStrength);
        if (n.m_windStrength < 1)
            n.m_windStrength = 1;
        n.m_radialAmount = shrink(m_radialAmount);
        if (n.m_radialAmount < 1)
            n.m_radialAmount = 1;
        n.m_surfaceRadius = shrink(m_surfaceRadius);
        if (n.m_surfaceRadius < 1)
            n.m_surfaceRadius = 1;
        n.m_smartRadius = shrink(m_smartRadius);
        if (n.m_smartRadius < 1)
            n.m_smartRadius = 1;
        return n;
    }

    /**
     * 该算子需要的输入外扩（像素）：点算子 = 0；盒模糊/USM = 半径；查找边缘/锐化/浮雕 = 1（3×3）；
     * 高斯模糊 = 半径 + 2（**安全上界**：三趟盒模糊半径之和 ≈ 0.98×半径，留 2px 余量，
     * 宁可比需要的外扩多一点，也不要因外扩不足在 64×64 块界留下接缝）；中值 = 半径。
     *
     * 注意本机制只能表达**对称**外扩 —— GIMP 的 `gimp:offset` 需要的是「平移后的通用矩形」
     * （见其 `get_required_for_output`），装不进来，故 offset 暂缓（设计与迁移步骤见 docs/pending-dev.md）。
     */
    int inputPad() const
    {
        switch (m_op) {
        case OpName::BoxBlur:
            return m_blurRadius;
        case OpName::UnsharpMask:
            return m_usmRadius;
        case OpName::FindEdges:
            return 1; // 3×3 Sobel
        case OpName::GaussianBlur:
            return m_gaussRadius + 2;
        case OpName::MedianBlur:
        case OpName::DustAndScratches:
        case OpName::ReduceNoise:
            return m_medianRadius;
        case OpName::Sharpen:
        case OpName::Emboss:
        case OpName::SharpenEdges:
            return 1; // 3×3 线性核 / Sobel
        case OpName::Maximum:
        case OpName::Minimum:
            return m_extremumRadius;
        case OpName::CustomFilter:
            return 1; // 3×3 卷积核
        case OpName::HighPass:
            return m_highPassRadius;
        case OpName::MotionBlur:
            return m_motionDistance; // 对称上界（requiredInputs 可更紧）
        case OpName::Ripple:
            return m_rippleAmplitude;
        case OpName::Fragment:
            return 4; // 四角偏移 ±4
        case OpName::Mosaic:
            return 0; // 位置相关，走整层（见 FilterStack::hasPositionalOp）
        case OpName::Wind:
            return (m_windMethod == 1) ? (m_windStrength * 2) : m_windStrength;
        case OpName::TraceContour:
            return 1; // 4-邻接
        case OpName::RadialBlur:
            return 0; // 位置相关，走整层（见 FilterStack::hasPositionalOp）
        case OpName::SurfaceBlur:
            return m_surfaceRadius;
        case OpName::SmartBlur:
            return m_smartRadius;
        case OpName::Tiles:
            return 0; // 位置相关，走整层（见 FilterStack::hasPositionalOp）
        default:
            return 0;
        }
    }

private:
    OpName m_op = OpName::BrightnessContrast;
    bool m_enabled = true;

    qreal m_brightness = 0.0;
    qreal m_contrast = 0.0;
    qreal m_hue = 0.0;
    qreal m_saturation = 0.0;
    qreal m_lightness = 0.0;
    qreal m_vibrance = 0.0;
    qreal m_exposure = 0.0;
    qreal m_exposureOffset = 0.0;
    qreal m_gammaCorrection = 1.0;
    qreal m_levelsBlack = 0.0;
    qreal m_levelsWhite = 255.0;
    qreal m_levelsGamma = 1.0;
    qreal m_colorBalanceCR = 0.0;
    qreal m_colorBalanceMG = 0.0;
    qreal m_colorBalanceYB = 0.0;
    qreal m_photoFilterHue = 35.0;
    qreal m_photoFilterDensity = 25.0;
    qreal m_bwReds = 40.0;
    qreal m_bwYellows = 60.0;
    qreal m_bwGreens = 40.0;
    qreal m_bwCyans = 60.0;
    qreal m_bwBlues = 20.0;
    qreal m_bwMagentas = 80.0;
    int m_posterizeLevels = 4;
    int m_threshold = 128;

    int m_curveY[5] = {0, 64, 128, 192, 255};
    qreal m_mix[9] = {100, 0, 0, 0, 100, 0, 0, 0, 100};
    bool m_mixMonochrome = false;
    int m_colorLookupPreset = 0;
    quint32 m_gradColorA = 0xff000000; ///< ARGB 黑
    quint32 m_gradColorB = 0xffffffff; ///< ARGB 白
    qreal m_gradStrength = 100.0;
    int m_selTarget = 0;
    qreal m_selC = 0.0;
    qreal m_selM = 0.0;
    qreal m_selY = 0.0;
    qreal m_selK = 0.0;
    int m_blurRadius = 0;
    int m_usmRadius = 2;
    qreal m_usmAmount = 1.0;   ///< 1.0 = 100%（PS 默认量级）
    int m_usmThreshold = 0;    ///< 0 = 全像素参与（PS 默认 0）
    int m_gaussRadius = 5;
    int m_medianRadius = 3;
    int m_sharpenStrength = 1;
    int m_embossStrength = 1;
    int m_offsetDx = 0;
    int m_offsetDy = 0;
    int m_extremumRadius = 1;
    int m_customW[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0}; ///< 默认恒等（中心 1）
    int m_customScale = 1;
    int m_customOffset = 0;
    int m_highPassRadius = 5;
    int m_motionDistance = 10;
    int m_motionAngle = 0;
    int m_noiseAmount = 12;
    bool m_noiseGaussian = false;
    bool m_noiseMonochromatic = true;
    quint32 m_noiseSeed = 0x9E3779B9u; ///< 固定默认：同参数 → 同噪声（见 noiseSeed() 注释）
    int m_reduceNoiseStrength = 50; ///< 减少杂色：与中值结果的混合强度 %
    int m_rippleAmplitude = 5;
    int m_rippleWavelength = 16;
    int m_mosaicCell = 8;
    int m_windDirection = 0;
    int m_windMethod = 0;
    int m_windStrength = 5;
    int m_contourLevel = 128;
    int m_contourEdge = 0;
    int m_radialAmount = 10;
    int m_radialMethod = 0;
    int m_surfaceRadius = 5;
    int m_surfaceThreshold = 15;
    int m_smartRadius = 3;
    int m_smartThreshold = 25;
    int m_smartMode = 0;
    int m_tilesCount = 10;
    int m_tilesMaxOffset = 10;
    int m_tilesFill = 0;
    quint32 m_tilesSeed = 0xC0FFEE01u;
};

} // namespace Ps

#endif // DOMAIN_FILTERNODE_H
