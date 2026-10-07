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
    setEnabled(true);
    m_title->setText(node.title());
    showPageFor(node.op());
    loadControlsFromNode();
}

void AdjustmentPropsHost::emitPreview()
{
    if (!m_block)
        emit previewChanged(m_node);
}

void AdjustmentPropsHost::emitCommit()
{
    if (!m_block)
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
                slider->blockSignals(true);
                slider->setValue(v);
                slider->blockSignals(false);
                applyToNode(v);
                emitPreview();
            });
    connect(slider, &QSlider::sliderReleased, this, &AdjustmentPropsHost::emitCommit);
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
    default:
        break;
    }
    m_block = false;
}
