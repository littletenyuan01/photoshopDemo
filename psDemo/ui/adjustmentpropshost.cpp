/**
 * adjustmentpropshost.cpp — 调整图层属性页宿主。
 */
#include "adjustmentpropshost.h"

#include "ui_adjbrightnesscontrast.h"
#include "ui_adjlevels.h"
#include "ui_adjcurves.h"
#include "ui_adjexposure.h"
#include "ui_adjvibrance.h"
#include "ui_adjhuesaturation.h"
#include "ui_adjcolorbalance.h"
#include "ui_adjblackandwhite.h"
#include "ui_adjphotofilter.h"
#include "ui_adjchannelmixer.h"
#include "ui_adjcolorlookup.h"
#include "ui_adjinvert.h"
#include "ui_adjposterize.h"
#include "ui_adjthreshold.h"
#include "ui_adjgradientmap.h"
#include "ui_adjselectivecolor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

template <typename UiT>
QWidget *makePage(QWidget *parent, UiT *ui)
{
    auto *page = new QWidget(parent);
    ui->setupUi(page);
    return page;
}

/** 程序化建的「标签 + 滑动条 + 数值框」一行（给没有 .ui 的参数页用）。 */
struct SliderRow {
    QWidget *row = nullptr;
    QSlider *slider = nullptr;
    QSpinBox *spin = nullptr;
};

SliderRow makeSliderRow(QWidget *parent, const QString &text, const QString &name, int lo, int hi)
{
    SliderRow r;
    r.row = new QWidget(parent);
    auto *lay = new QHBoxLayout(r.row);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(text, r.row);
    r.slider = new QSlider(Qt::Horizontal, r.row);
    r.slider->setRange(lo, hi);
    r.slider->setObjectName(QStringLiteral("slider_") + name);
    r.spin = new QSpinBox(r.row);
    r.spin->setRange(lo, hi);
    r.spin->setObjectName(QStringLiteral("spin_") + name);
    lay->addWidget(label);
    lay->addWidget(r.slider, 1);
    lay->addWidget(r.spin);
    return r;
}

} // namespace

AdjustmentPropsHost::AdjustmentPropsHost(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    m_title = new QLabel(this);
    m_title->setWordWrap(true);
    m_stack = new QStackedWidget(this);
    lay->addWidget(m_title);
    lay->addWidget(m_stack, 1);
    clear();
}

void AdjustmentPropsHost::clear()
{
    m_node = Ps::FilterNode();
    m_title->setText(QStringLiteral("未选中调整图层"));
    if (m_stack->count() > 0)
        m_stack->setCurrentIndex(0);
    setEnabled(false);
}

void AdjustmentPropsHost::setNode(const Ps::FilterNode &node)
{
    ensurePages();
    m_node = node;
    m_pendingPreview = false;
    setEnabled(true);
    m_title->setText(node.title());
    showPageFor(node.op());
    loadControlsFromNode();
}

void AdjustmentPropsHost::emitPreview()
{
    if (m_block)
        return;
    m_pendingPreview = true;
    emit previewChanged(m_node);
}

void AdjustmentPropsHost::emitCommit()
{
    // 只有「确实有过未提交的预览改动」才提交：避免 sliderReleased 与数值框
    // editingFinished 相继触发时产生重复的 undo 条目。
    if (m_block || !m_pendingPreview)
        return;
    m_pendingPreview = false;
    emit commitChanged(m_node);
}

void AdjustmentPropsHost::wireSliderSpin(QSlider *slider, QSpinBox *spin,
                                         const std::function<void(int)> &applyToNode)
{
    if (!slider || !spin)
        return;
    spin->setRange(slider->minimum(), slider->maximum());
    connect(slider, &QSlider::valueChanged, this, [this, spin, applyToNode](int v) {
        if (m_block)
            return;
        spin->blockSignals(true);
        spin->setValue(v);
        spin->blockSignals(false);
        applyToNode(v);
        emitPreview();
    });
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, slider, applyToNode](int v) {
                if (m_block)
                    return;
                // 滑条与数值框始终同步，故 slider->value() 就是「已应用值」。
                // Qt 的 spinbox 在场编辑收尾（interpret）时会以 always-emit 策略**重发同值**
                // valueChanged；不挡掉的话，回车一次就会多出一条空操作的预览 + undo 条目。
                if (slider->value() == v)
                    return;
                slider->blockSignals(true);
                slider->setValue(v);
                slider->blockSignals(false);
                applyToNode(v);
                emitPreview();
            });
    connect(slider, &QSlider::sliderReleased, this, &AdjustmentPropsHost::emitCommit);
    // 数值框输入/回车/失焦也要提交：否则「在框里输值」只走预览 —— 不产生 undo、
    // 也不把文档标记为已修改，关窗时可能不提示保存，用户的改动就丢了。
    connect(spin, &QSpinBox::editingFinished, this, &AdjustmentPropsHost::emitCommit);
    connect(spin, &QSpinBox::editingFinished, this, &AdjustmentPropsHost::emitCommit);
}

void AdjustmentPropsHost::ensurePages()
{
    if (m_pagesReady)
        return;
    m_pagesReady = true;

    auto add = [this](Ps::OpName op, QWidget *page) {
        m_opToPage.insert(int(op), m_stack->addWidget(page));
    };

    {
        auto *ui = new Ui::AdjBrightnessContrast;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_brightness, ui->spin_brightness, [this](int v) {
            m_node.setBrightness(v / 100.0);
        });
        wireSliderSpin(ui->slider_contrast, ui->spin_contrast, [this](int v) {
            m_node.setContrast(v / 100.0);
        });
        add(Ps::OpName::BrightnessContrast, page);
    }
    {
        auto *ui = new Ui::AdjLevels;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_black, ui->spin_black, [this](int v) {
            m_node.setLevelsBlack(v);
        });
        wireSliderSpin(ui->slider_white, ui->spin_white, [this](int v) {
            m_node.setLevelsWhite(v);
        });
        wireSliderSpin(ui->slider_gamma, ui->spin_gamma, [this](int v) {
            m_node.setLevelsGamma(v / 100.0);
        });
        add(Ps::OpName::Levels, page);
    }
    {
        auto *ui = new Ui::AdjCurves;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_y0, ui->spin_y0, [this](int v) { m_node.setCurveY0(v); });
        wireSliderSpin(ui->slider_y1, ui->spin_y1, [this](int v) { m_node.setCurveY1(v); });
        wireSliderSpin(ui->slider_y2, ui->spin_y2, [this](int v) { m_node.setCurveY2(v); });
        wireSliderSpin(ui->slider_y3, ui->spin_y3, [this](int v) { m_node.setCurveY3(v); });
        wireSliderSpin(ui->slider_y4, ui->spin_y4, [this](int v) { m_node.setCurveY4(v); });
        add(Ps::OpName::Curves, page);
    }
    {
        auto *ui = new Ui::AdjExposure;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_exposure, ui->spin_exposure, [this](int v) {
            m_node.setExposure(v / 100.0);
        });
        wireSliderSpin(ui->slider_offset, ui->spin_offset, [this](int v) {
            m_node.setExposureOffset(v / 100.0);
        });
        wireSliderSpin(ui->slider_gamma, ui->spin_gamma, [this](int v) {
            m_node.setGammaCorrection(v / 100.0);
        });
        add(Ps::OpName::Exposure, page);
    }
    {
        auto *ui = new Ui::AdjVibrance;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_vibrance, ui->spin_vibrance, [this](int v) {
            m_node.setVibrance(v);
        });
        wireSliderSpin(ui->slider_saturation, ui->spin_saturation, [this](int v) {
            m_node.setSaturation(v);
        });
        add(Ps::OpName::Vibrance, page);
    }
    {
        auto *ui = new Ui::AdjHueSaturation;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_hue, ui->spin_hue, [this](int v) {
            m_node.setHue(v);
        });
        wireSliderSpin(ui->slider_saturation, ui->spin_saturation, [this](int v) {
            m_node.setSaturation(v);
        });
        wireSliderSpin(ui->slider_lightness, ui->spin_lightness, [this](int v) {
            m_node.setLightness(v);
        });
        add(Ps::OpName::HueSaturation, page);
    }
    {
        auto *ui = new Ui::AdjColorBalance;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_cr, ui->spin_cr, [this](int v) {
            m_node.setColorBalanceCR(v);
        });
        wireSliderSpin(ui->slider_mg, ui->spin_mg, [this](int v) {
            m_node.setColorBalanceMG(v);
        });
        wireSliderSpin(ui->slider_yb, ui->spin_yb, [this](int v) {
            m_node.setColorBalanceYB(v);
        });
        add(Ps::OpName::ColorBalance, page);
    }
    {
        auto *ui = new Ui::AdjBlackAndWhite;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_reds, ui->spin_reds, [this](int v) {
            m_node.setBwReds(v);
        });
        wireSliderSpin(ui->slider_yellows, ui->spin_yellows, [this](int v) {
            m_node.setBwYellows(v);
        });
        wireSliderSpin(ui->slider_greens, ui->spin_greens, [this](int v) {
            m_node.setBwGreens(v);
        });
        wireSliderSpin(ui->slider_cyans, ui->spin_cyans, [this](int v) {
            m_node.setBwCyans(v);
        });
        wireSliderSpin(ui->slider_blues, ui->spin_blues, [this](int v) {
            m_node.setBwBlues(v);
        });
        wireSliderSpin(ui->slider_magentas, ui->spin_magentas, [this](int v) {
            m_node.setBwMagentas(v);
        });
        add(Ps::OpName::BlackAndWhite, page);
    }
    {
        auto *ui = new Ui::AdjPhotoFilter;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_hue, ui->spin_hue, [this](int v) {
            m_node.setPhotoFilterHue(v);
        });
        wireSliderSpin(ui->slider_density, ui->spin_density, [this](int v) {
            m_node.setPhotoFilterDensity(v);
        });
        add(Ps::OpName::PhotoFilter, page);
    }
    {
        auto *ui = new Ui::AdjChannelMixer;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_rr, ui->spin_rr, [this](int v) { m_node.setMixRr(v); });
        wireSliderSpin(ui->slider_rg, ui->spin_rg, [this](int v) { m_node.setMixRg(v); });
        wireSliderSpin(ui->slider_rb, ui->spin_rb, [this](int v) { m_node.setMixRb(v); });
        wireSliderSpin(ui->slider_gr, ui->spin_gr, [this](int v) { m_node.setMixGr(v); });
        wireSliderSpin(ui->slider_gg, ui->spin_gg, [this](int v) { m_node.setMixGg(v); });
        wireSliderSpin(ui->slider_gb, ui->spin_gb, [this](int v) { m_node.setMixGb(v); });
        wireSliderSpin(ui->slider_br, ui->spin_br, [this](int v) { m_node.setMixBr(v); });
        wireSliderSpin(ui->slider_bg, ui->spin_bg, [this](int v) { m_node.setMixBg(v); });
        wireSliderSpin(ui->slider_bb, ui->spin_bb, [this](int v) { m_node.setMixBb(v); });
        connect(ui->checkMono, &QCheckBox::toggled, this, [this](bool on) {
            if (m_block)
                return;
            m_node.setMixMonochrome(on);
            emitPreview();
            emitCommit();
        });
        add(Ps::OpName::ChannelMixer, page);
    }
    {
        auto *ui = new Ui::AdjColorLookup;
        QWidget *page = makePage(m_stack, ui);
        connect(ui->comboPreset, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int idx) {
                    if (m_block)
                        return;
                    m_node.setColorLookupPreset(idx);
                    emitPreview();
                    emitCommit();
                });
        add(Ps::OpName::ColorLookup, page);
    }
    {
        auto *ui = new Ui::AdjInvert;
        add(Ps::OpName::Invert, makePage(m_stack, ui));
    }
    {
        auto *ui = new Ui::AdjPosterize;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_levels, ui->spin_levels, [this](int v) {
            m_node.setPosterizeLevels(v);
        });
        add(Ps::OpName::Posterize, page);
    }
    {
        auto *ui = new Ui::AdjThreshold;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_threshold, ui->spin_threshold, [this](int v) {
            m_node.setThreshold(v);
        });
        add(Ps::OpName::Threshold, page);
    }
    {
        auto *ui = new Ui::AdjGradientMap;
        QWidget *page = makePage(m_stack, ui);
        wireSliderSpin(ui->slider_strength, ui->spin_strength, [this](int v) {
            m_node.setGradientMapStrength(v);
        });
        add(Ps::OpName::GradientMap, page);
    }
    {
        auto *ui = new Ui::AdjSelectiveColor;
        QWidget *page = makePage(m_stack, ui);
        connect(ui->comboTarget, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int idx) {
                    if (m_block)
                        return;
                    m_node.setSelectiveColorTarget(idx);
                    emitPreview();
                    emitCommit();
                });
        wireSliderSpin(ui->slider_c, ui->spin_c, [this](int v) { m_node.setSelectiveCyan(v); });
        wireSliderSpin(ui->slider_m, ui->spin_m, [this](int v) { m_node.setSelectiveMagenta(v); });
        wireSliderSpin(ui->slider_y, ui->spin_y, [this](int v) { m_node.setSelectiveYellow(v); });
        wireSliderSpin(ui->slider_k, ui->spin_k, [this](int v) { m_node.setSelectiveBlack(v); });
        add(Ps::OpName::SelectiveColor, page);
    }
    {
        // 盒模糊（面积算子）：无 .ui，程序化建一页（半径 0..128）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("radius"), 0, 128);
        col->addWidget(radius.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setBlurRadius(v); });
        add(Ps::OpName::BoxBlur, page);
    }
    {
        // USM 锐化（面积算子）：无 .ui，程序化建一页
        // 强度用百分比整数表示（0..500 → 0.0..5.0），与其它页「整数控件 + qreal 参数」的口径一致
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("usm_radius"), 1, 64);
        const SliderRow amount = makeSliderRow(page, QStringLiteral("强度"), QStringLiteral("usm_amount"), 0, 500);
        const SliderRow threshold = makeSliderRow(page, QStringLiteral("阈值"), QStringLiteral("usm_threshold"), 0, 255);
        col->addWidget(radius.row);
        col->addWidget(amount.row);
        col->addWidget(threshold.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setUsmRadius(v); });
        wireSliderSpin(amount.slider, amount.spin, [this](int v) { m_node.setUsmAmount(v / 100.0); });
        wireSliderSpin(threshold.slider, threshold.spin, [this](int v) { m_node.setUsmThreshold(v); });
        add(Ps::OpName::UnsharpMask, page);
    }
    {
        // 高斯模糊（面积算子）：无 .ui，一页一个半径滑块（σ = 半径/3，见 gaussianblurfilterop.h）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("gauss_radius"), 1, 64);
        col->addWidget(radius.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setGaussRadius(v); });
        add(Ps::OpName::GaussianBlur, page);
    }
    {
        // 中间值 / 中值模糊（面积算子）：无 .ui，一页一个半径滑块（1..8）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("median_radius"), 1, 8);
        col->addWidget(radius.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setMedianRadius(v); });
        add(Ps::OpName::MedianBlur, page);
    }
    {
        // 锐化（面积算子）：无 .ui，一页一个强度滑块（1..4；1 = 经典锐化）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow strength = makeSliderRow(page, QStringLiteral("强度"), QStringLiteral("sharpen_strength"), 1, 4);
        col->addWidget(strength.row);
        wireSliderSpin(strength.slider, strength.spin, [this](int v) { m_node.setSharpenStrength(v); });
        add(Ps::OpName::Sharpen, page);
    }
    {
        // 浮雕效果（面积算子）：无 .ui，一页一个强度滑块（1..4）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow strength = makeSliderRow(page, QStringLiteral("强度"), QStringLiteral("emboss_strength"), 1, 4);
        col->addWidget(strength.row);
        wireSliderSpin(strength.slider, strength.spin, [this](int v) { m_node.setEmbossStrength(v); });
        col->addWidget(strength.row);
        add(Ps::OpName::Emboss, page);
    }
    {
        // 位移：无 .ui，一页两个偏移滑块（移出即丢、空出透明）。
        // 范围取 ±512（够覆盖常见画布；`FilterNode` 内部钳到 ±2048）。
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow dx = makeSliderRow(page, QStringLiteral("水平"), QStringLiteral("offset_dx"), -512, 512);
        const SliderRow dy = makeSliderRow(page, QStringLiteral("垂直"), QStringLiteral("offset_dy"), -512, 512);
        col->addWidget(dx.row);
        col->addWidget(dy.row);
        wireSliderSpin(dx.slider, dx.spin, [this](int v) { m_node.setOffsetDx(v); });
        wireSliderSpin(dy.slider, dy.spin, [this](int v) { m_node.setOffsetDy(v); });
        add(Ps::OpName::Offset, page);
    }
    {
        // 最大值 / 最小值（排序统计滤波）：与中值同构，一页一个半径滑块（1..8）
        for (bool isMax : {true, false}) {
            auto *page = new QWidget(m_stack);
            auto *col = new QVBoxLayout(page);
            col->setContentsMargins(0, 0, 0, 0);
            const QString name = isMax ? QStringLiteral("maximum_radius")
                                       : QStringLiteral("minimum_radius");
            const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), name, 1, 8);
            col->addWidget(radius.row);
            wireSliderSpin(radius.slider, radius.spin,
                           [this](int v) { m_node.setExtremumRadius(v); });
            add(isMax ? Ps::OpName::Maximum : Ps::OpName::Minimum, page);
        }
    }
    {
        // 自定滤镜（3×3 卷积核 + 缩放/位移）：无 .ui，程序化建 3×3 权重网格
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        for (int r = 0; r < 3; ++r) {
            auto *row = new QWidget(page);
            auto *rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(0, 0, 0, 0);
            for (int c = 0; c < 3; ++c) {
                auto *sp = new QSpinBox(row);
                sp->setRange(-999, 999);
                sp->setObjectName(QStringLiteral("spin_custom_%1").arg(r * 3 + c));
                const int idx = r * 3 + c;
                connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, [this, idx](int v) {
                    if (m_block)
                        return;
                    m_node.setCustomWeight(idx, v);
                    emitPreview();
                });
                connect(sp, &QSpinBox::editingFinished, this, &AdjustmentPropsHost::emitCommit);
                rowLay->addWidget(sp);
            }
            col->addWidget(row);
        }
        const SliderRow scale = makeSliderRow(page, QStringLiteral("缩放"), QStringLiteral("custom_scale"), 0, 9999);
        const SliderRow off = makeSliderRow(page, QStringLiteral("位移"), QStringLiteral("custom_offset"), -255, 255);
        col->addWidget(scale.row);
        col->addWidget(off.row);
        wireSliderSpin(scale.slider, scale.spin, [this](int v) { m_node.setCustomScale(v); });
        wireSliderSpin(off.slider, off.spin, [this](int v) { m_node.setCustomOffset(v); });
        add(Ps::OpName::CustomFilter, page);
    }
    {
        // 高通保留：一页一个半径滑块（1..64；模糊级复用盒模糊）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("highpass_radius"), 1, 64);
        col->addWidget(radius.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setHighPassRadius(v); });
        add(Ps::OpName::HighPass, page);
    }
    {
        // 运动模糊：距离滑块（1..128）+ 角度下拉（0°/45°/90°）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow dist = makeSliderRow(page, QStringLiteral("距离"), QStringLiteral("motion_distance"), 1, 128);
        col->addWidget(dist.row);
        wireSliderSpin(dist.slider, dist.spin, [this](int v) { m_node.setMotionDistance(v); });
        auto *row = new QWidget(page);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(new QLabel(QStringLiteral("角度"), row));
        auto *combo = new QComboBox(row);
        combo->setObjectName(QStringLiteral("combo_motion_angle"));
        combo->addItem(QStringLiteral("水平 0°"), 0);
        combo->addItem(QStringLiteral("斜向 45°"), 45);
        combo->addItem(QStringLiteral("垂直 90°"), 90);
        rowLay->addWidget(combo, 1);
        col->addWidget(row);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
            if (m_block)
                return;
            m_node.setMotionAngle(combo->currentData().toInt());
            emitPreview();
        });
        add(Ps::OpName::MotionBlur, page);
    }
    {
        // 添加杂色：数量滑块（0..100%）+ 分布下拉 + 单色勾选
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow amt = makeSliderRow(page, QStringLiteral("数量 %"), QStringLiteral("noise_amount"), 0, 100);
        col->addWidget(amt.row);
        wireSliderSpin(amt.slider, amt.spin, [this](int v) { m_node.setNoiseAmount(v); });

        auto *row = new QWidget(page);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(new QLabel(QStringLiteral("分布"), row));
        auto *combo = new QComboBox(row);
        combo->setObjectName(QStringLiteral("combo_noise_dist"));
        combo->addItem(QStringLiteral("平均分布"), 0);
        combo->addItem(QStringLiteral("高斯分布"), 1);
        rowLay->addWidget(combo, 1);
        col->addWidget(row);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
            if (m_block)
                return;
            m_node.setNoiseGaussian(combo->currentIndex() == 1);
            emitPreview();
        });

        auto *mono = new QCheckBox(QStringLiteral("单色（三通道加同一个扰动）"), page);
        mono->setObjectName(QStringLiteral("check_noise_mono"));
        col->addWidget(mono);
        connect(mono, &QCheckBox::toggled, this, [this](bool on) {
            if (m_block)
                return;
            m_node.setNoiseMonochromatic(on);
            emitPreview();
        });
        add(Ps::OpName::AddNoise, page);
    }
    {
        // 蒙尘与划痕：半径 + 阈值（Demo：中值，差超阈值才替换）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("dust_radius"), 1, 8);
        const SliderRow thr = makeSliderRow(page, QStringLiteral("阈值"), QStringLiteral("dust_threshold"), 0, 255);
        col->addWidget(radius.row);
        col->addWidget(thr.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setMedianRadius(v); });
        wireSliderSpin(thr.slider, thr.spin, [this](int v) { m_node.setThreshold(v); });
        add(Ps::OpName::DustAndScratches, page);
    }
    {
        // 减少杂色：半径 + 强度%（Demo：中值混合）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow radius = makeSliderRow(page, QStringLiteral("半径"), QStringLiteral("reduce_radius"), 1, 8);
        const SliderRow str = makeSliderRow(page, QStringLiteral("强度 %"), QStringLiteral("reduce_strength"), 0, 100);
        col->addWidget(radius.row);
        col->addWidget(str.row);
        wireSliderSpin(radius.slider, radius.spin, [this](int v) { m_node.setMedianRadius(v); });
        wireSliderSpin(str.slider, str.spin, [this](int v) { m_node.setReduceNoiseStrength(v); });
        add(Ps::OpName::ReduceNoise, page);
    }
    {
        // 锐化边缘：强度（复用锐化强度）
        auto *page = new QWidget(m_stack);
        auto *col = new QVBoxLayout(page);
        col->setContentsMargins(0, 0, 0, 0);
        const SliderRow strength = makeSliderRow(page, QStringLiteral("强度"), QStringLiteral("sharpen_edges_strength"), 1, 4);
        col->addWidget(strength.row);
        wireSliderSpin(strength.slider, strength.spin, [this](int v) { m_node.setSharpenStrength(v); });
        add(Ps::OpName::SharpenEdges, page);
    }
}

void AdjustmentPropsHost::showPageFor(Ps::OpName op)
{
    const int page = m_opToPage.value(int(op), 0);
    m_stack->setCurrentIndex(page);
}

void AdjustmentPropsHost::loadControlsFromNode()
{
    m_block = true;
    QWidget *page = m_stack->currentWidget();
    if (!page) {
        m_block = false;
        return;
    }

    auto setPair = [&](const char *sliderName, const char *spinName, int value) {
        if (auto *s = page->findChild<QSlider *>(QString::fromUtf8(sliderName)))
            s->setValue(value);
        if (auto *sp = page->findChild<QSpinBox *>(QString::fromUtf8(spinName)))
            sp->setValue(value);
    };

    switch (m_node.op()) {
    case Ps::OpName::BrightnessContrast:
        setPair("slider_brightness", "spin_brightness", int(m_node.brightness() * 100));
        setPair("slider_contrast", "spin_contrast", int(m_node.contrast() * 100));
        break;
    case Ps::OpName::Levels:
        setPair("slider_black", "spin_black", int(m_node.levelsBlack()));
        setPair("slider_white", "spin_white", int(m_node.levelsWhite()));
        setPair("slider_gamma", "spin_gamma", int(m_node.levelsGamma() * 100));
        break;
    case Ps::OpName::Exposure:
        setPair("slider_exposure", "spin_exposure", int(m_node.exposure() * 100));
        setPair("slider_offset", "spin_offset", int(m_node.exposureOffset() * 100));
        setPair("slider_gamma", "spin_gamma", int(m_node.gammaCorrection() * 100));
        break;
    case Ps::OpName::Vibrance:
        setPair("slider_vibrance", "spin_vibrance", int(m_node.vibrance()));
        setPair("slider_saturation", "spin_saturation", int(m_node.saturation()));
        break;
    case Ps::OpName::HueSaturation:
        setPair("slider_hue", "spin_hue", int(m_node.hue()));
        setPair("slider_saturation", "spin_saturation", int(m_node.saturation()));
        setPair("slider_lightness", "spin_lightness", int(m_node.lightness()));
        break;
    case Ps::OpName::ColorBalance:
        setPair("slider_cr", "spin_cr", int(m_node.colorBalanceCR()));
        setPair("slider_mg", "spin_mg", int(m_node.colorBalanceMG()));
        setPair("slider_yb", "spin_yb", int(m_node.colorBalanceYB()));
        break;
    case Ps::OpName::BlackAndWhite:
        setPair("slider_reds", "spin_reds", int(m_node.bwReds()));
        setPair("slider_yellows", "spin_yellows", int(m_node.bwYellows()));
        setPair("slider_greens", "spin_greens", int(m_node.bwGreens()));
        setPair("slider_cyans", "spin_cyans", int(m_node.bwCyans()));
        setPair("slider_blues", "spin_blues", int(m_node.bwBlues()));
        setPair("slider_magentas", "spin_magentas", int(m_node.bwMagentas()));
        break;
    case Ps::OpName::PhotoFilter:
        setPair("slider_hue", "spin_hue", int(m_node.photoFilterHue()));
        setPair("slider_density", "spin_density", int(m_node.photoFilterDensity()));
        break;
    case Ps::OpName::Posterize:
        setPair("slider_levels", "spin_levels", m_node.posterizeLevels());
        break;
    case Ps::OpName::Threshold:
        setPair("slider_threshold", "spin_threshold", m_node.threshold());
        break;
    case Ps::OpName::Curves:
        setPair("slider_y0", "spin_y0", m_node.curveY0());
        setPair("slider_y1", "spin_y1", m_node.curveY1());
        setPair("slider_y2", "spin_y2", m_node.curveY2());
        setPair("slider_y3", "spin_y3", m_node.curveY3());
        setPair("slider_y4", "spin_y4", m_node.curveY4());
        break;
    case Ps::OpName::ChannelMixer:
        setPair("slider_rr", "spin_rr", int(m_node.mixRr()));
        setPair("slider_rg", "spin_rg", int(m_node.mixRg()));
        setPair("slider_rb", "spin_rb", int(m_node.mixRb()));
        setPair("slider_gr", "spin_gr", int(m_node.mixGr()));
        setPair("slider_gg", "spin_gg", int(m_node.mixGg()));
        setPair("slider_gb", "spin_gb", int(m_node.mixGb()));
        setPair("slider_br", "spin_br", int(m_node.mixBr()));
        setPair("slider_bg", "spin_bg", int(m_node.mixBg()));
        setPair("slider_bb", "spin_bb", int(m_node.mixBb()));
        if (auto *c = page->findChild<QCheckBox *>(QStringLiteral("checkMono")))
            c->setChecked(m_node.mixMonochrome());
        break;
    case Ps::OpName::ColorLookup:
        if (auto *cb = page->findChild<QComboBox *>(QStringLiteral("comboPreset")))
            cb->setCurrentIndex(m_node.colorLookupPreset());
        break;
    case Ps::OpName::GradientMap:
        setPair("slider_strength", "spin_strength", int(m_node.gradientMapStrength()));
        break;
    case Ps::OpName::SelectiveColor:
        if (auto *cb = page->findChild<QComboBox *>(QStringLiteral("comboTarget")))
            cb->setCurrentIndex(m_node.selectiveColorTarget());
        setPair("slider_c", "spin_c", int(m_node.selectiveCyan()));
        setPair("slider_m", "spin_m", int(m_node.selectiveMagenta()));
        setPair("slider_y", "spin_y", int(m_node.selectiveYellow()));
        setPair("slider_k", "spin_k", int(m_node.selectiveBlack()));
        break;
    case Ps::OpName::BoxBlur:
        setPair("slider_radius", "spin_radius", m_node.blurRadius());
        break;
    case Ps::OpName::UnsharpMask:
        setPair("slider_usm_radius", "spin_usm_radius", m_node.usmRadius());
        setPair("slider_usm_amount", "spin_usm_amount", int(m_node.usmAmount() * 100.0 + 0.5));
        setPair("slider_usm_threshold", "spin_usm_threshold", m_node.usmThreshold());
        break;
    case Ps::OpName::GaussianBlur:
        setPair("slider_gauss_radius", "spin_gauss_radius", m_node.gaussRadius());
        break;
    case Ps::OpName::MedianBlur:
        setPair("slider_median_radius", "spin_median_radius", m_node.medianRadius());
        break;
    case Ps::OpName::Sharpen:
        setPair("slider_sharpen_strength", "spin_sharpen_strength", m_node.sharpenStrength());
        break;
    case Ps::OpName::Emboss:
        setPair("slider_emboss_strength", "spin_emboss_strength", m_node.embossStrength());
        break;
    case Ps::OpName::Offset:
        setPair("slider_offset_dx", "spin_offset_dx", m_node.offsetDx());
        setPair("slider_offset_dy", "spin_offset_dy", m_node.offsetDy());
        break;
    case Ps::OpName::Maximum:
    case Ps::OpName::Minimum:
        setPair("slider_maximum_radius", "spin_maximum_radius", m_node.extremumRadius());
        setPair("slider_minimum_radius", "spin_minimum_radius", m_node.extremumRadius());
        break;
    case Ps::OpName::CustomFilter:
        for (int i = 0; i < 9; ++i) {
            if (auto *sp = page->findChild<QSpinBox *>(QStringLiteral("spin_custom_%1").arg(i)))
                sp->setValue(m_node.customWeight(i));
        }
        setPair("slider_custom_scale", "spin_custom_scale", m_node.customScale());
        setPair("slider_custom_offset", "spin_custom_offset", m_node.customOffset());
        break;
    case Ps::OpName::HighPass:
        setPair("slider_highpass_radius", "spin_highpass_radius", m_node.highPassRadius());
        break;
    case Ps::OpName::MotionBlur:
        setPair("slider_motion_distance", "spin_motion_distance", m_node.motionDistance());
        if (auto *cb = page->findChild<QComboBox *>(QStringLiteral("combo_motion_angle"))) {
            const int idx = cb->findData(m_node.motionAngle());
            cb->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        break;
    case Ps::OpName::AddNoise:
        setPair("slider_noise_amount", "spin_noise_amount", m_node.noiseAmount());
        if (auto *cb = page->findChild<QComboBox *>(QStringLiteral("combo_noise_dist")))
            cb->setCurrentIndex(m_node.noiseGaussian() ? 1 : 0);
        if (auto *ck = page->findChild<QCheckBox *>(QStringLiteral("check_noise_mono")))
            ck->setChecked(m_node.noiseMonochromatic());
        break;
    case Ps::OpName::DustAndScratches:
        setPair("slider_dust_radius", "spin_dust_radius", m_node.medianRadius());
        setPair("slider_dust_threshold", "spin_dust_threshold", m_node.threshold());
        break;
    case Ps::OpName::ReduceNoise:
        setPair("slider_reduce_radius", "spin_reduce_radius", m_node.medianRadius());
        setPair("slider_reduce_strength", "spin_reduce_strength", m_node.reduceNoiseStrength());
        break;
    case Ps::OpName::SharpenEdges:
        setPair("slider_sharpen_edges_strength", "spin_sharpen_edges_strength", m_node.sharpenStrength());
        break;
    default:
        break;
    }
    m_block = false;
}
