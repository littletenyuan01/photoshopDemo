/**
 * filternode.h — 图层滤镜节点：OpName + 开关 + 参数（domain 层）。
 *
 * 非破坏；求值在 FilterStack::apply，不写回 Layer 瓦片。
 */
#ifndef DOMAIN_FILTERNODE_H
#define DOMAIN_FILTERNODE_H

#include "engine/op/opname.h"

#include <QString>
#include <QtGlobal>

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
};

} // namespace Ps

#endif // DOMAIN_FILTERNODE_H
