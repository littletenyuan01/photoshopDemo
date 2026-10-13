/**
 * t_adjustpanel.cpp — 调整层属性面板：参数回填 + 预览/提交链路（离屏，不需要真窗口）。
 *
 * 两条要守住的点：
 *  ① 选中调整层时，已存参数必须回填到滑动条/数值框（含 gamma 这种会被量程夹住的字段）；
 *  ② 改值只走预览；交互结束（松手 / 数值框回车）必须提交一次 undo，
 *     且不得重复提交 —— 否则「改过但没记进文档」会在关窗时静默丢失。
 *
 * 注意：同名控件在多个页面都存在（色阶与曝光都有 slider_gamma），
 * 必须按**当前页**查找，全树 findChild 会命中别的页而测出假结果。
 */
#include "domain/filternode.h"
#include "testutil.h"
#include "ui/adjustmentpropshost.h"

#include <QApplication>
#include <QKeyEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>

using namespace Ps;
using PsTests::Reporter;

namespace {

QWidget *currentPage(AdjustmentPropsHost &host)
{
    auto *stack = host.findChild<QStackedWidget *>();
    return stack ? stack->currentWidget() : nullptr;
}

int sliderValue(QWidget *page, const char *name)
{
    if (!page)
        return -9999;
    auto *s = page->findChild<QSlider *>(QString::fromUtf8(name));
    return s ? s->value() : -9999;
}

int spinValue(QWidget *page, const char *name)
{
    if (!page)
        return -9999;
    auto *sp = page->findChild<QSpinBox *>(QString::fromUtf8(name));
    return sp ? sp->value() : -9999;
}

} // namespace

int tAdjustPanel()
{
    Reporter r;

    AdjustmentPropsHost host;
    int previews = 0, commits = 0;
    QObject::connect(&host, &AdjustmentPropsHost::previewChanged,
                     [&](const FilterNode &) { ++previews; });
    QObject::connect(&host, &AdjustmentPropsHost::commitChanged,
                     [&](const FilterNode &) { ++commits; });

    // —— 亮度/对比度回填 ——
    FilterNode bc(OpName::BrightnessContrast);
    bc.setBrightness(0.08);
    bc.setContrast(0.10);
    host.setNode(bc);
    QWidget *page = currentPage(host);
    r.check(page != nullptr, QStringLiteral("setNode 后切到了参数页"));
    r.check(sliderValue(page, "slider_brightness") == 8
                && spinValue(page, "spin_brightness") == 8,
            QStringLiteral("亮度 0.08 → 滑动条/数值框回填 8"));
    r.check(sliderValue(page, "slider_contrast") == 10
                && spinValue(page, "spin_contrast") == 10,
            QStringLiteral("对比度 0.10 → 回填 10"));

    // —— 色阶回填（含 gamma 量程）——
    FilterNode lv(OpName::Levels);
    lv.setLevelsBlack(137.0);
    lv.setLevelsWhite(168.0);
    lv.setLevelsGamma(4.79);
    host.setNode(lv);
    page = currentPage(host);
    r.check(sliderValue(page, "slider_black") == 137 && spinValue(page, "spin_black") == 137,
            QStringLiteral("色阶黑场 137 → 回填 137"));
    r.check(sliderValue(page, "slider_white") == 168 && spinValue(page, "spin_white") == 168,
            QStringLiteral("色阶白场 168 → 回填 168"));
    r.check(sliderValue(page, "slider_gamma") == 479 && spinValue(page, "spin_gamma") == 479,
            QStringLiteral("色阶 gamma 4.79 → 回填 479（未被量程夹住）"));

    // —— 预览/提交链路 ——
    auto *spin = page ? page->findChild<QSpinBox *>(QStringLiteral("spin_black")) : nullptr;
    if (!spin) {
        r.check(false, QStringLiteral("找到 spin_black"));
        return r.fails;
    }
    const int p0 = previews, c0 = commits;
    spin->setValue(spin->value() + 3);
    r.check(previews - p0 >= 1 && commits - c0 == 0,
            QStringLiteral("改数值框只产生预览、不产生 undo"));
    QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(spin, &ret);
    r.check(commits - c0 == 1, QStringLiteral("回车提交恰好一次（去重生效）"));

    // 再敲一次回车：没有新的改动 → 不应产生多余的 undo 条目
    const int p1 = previews, c1 = commits;
    const int vBefore = spin->value();
    const double hvBefore = host.node().levelsBlack();
    QApplication::sendEvent(spin, &ret);
    r.info(QStringLiteral("重复回车：spin %1→%2，node %3→%4，preview+%5 commit+%6")
               .arg(vBefore)
               .arg(spin->value())
               .arg(hvBefore, 0, 'f', 1)
               .arg(host.node().levelsBlack(), 0, 'f', 1)
               .arg(previews - p1)
               .arg(commits - c1));
    r.check(commits == c1, QStringLiteral("无新改动时重复回车不再提交"));

    // —— 盒模糊页（面积算子，程序化建页、无 .ui）——
    // 放在最后：setNode 会切换「当前页」，不能插在色阶页断言之间
    FilterNode bl(OpName::BoxBlur);
    bl.setBlurRadius(7);
    host.setNode(bl);
    QWidget *blPage = currentPage(host);
    r.check(sliderValue(blPage, "slider_radius") == 7
                && spinValue(blPage, "spin_radius") == 7,
            QStringLiteral("盒模糊半径 7 → 回填 7"));

    // —— 查找边缘 / 曝光过度 / 碎片（原先无页或无滑条）——
    FilterNode fe(OpName::FindEdges);
    fe.setUsmAmount(1.5);
    host.setNode(fe);
    QWidget *fePage = currentPage(host);
    r.check(sliderValue(fePage, "slider_findedges_amount") == 150
                && spinValue(fePage, "spin_findedges_amount") == 150,
            QStringLiteral("查找边缘强度 150% → 回填"));

    FilterNode sol(OpName::Solarize);
    sol.setThreshold(96);
    host.setNode(sol);
    QWidget *solPage = currentPage(host);
    r.check(sliderValue(solPage, "slider_solarize_threshold") == 96
                && spinValue(solPage, "spin_solarize_threshold") == 96,
            QStringLiteral("曝光过度阈值 96 → 回填"));

    FilterNode frag(OpName::Fragment);
    frag.setFragmentOffset(7);
    host.setNode(frag);
    QWidget *fragPage = currentPage(host);
    r.check(sliderValue(fragPage, "slider_fragment_offset") == 7
                && spinValue(fragPage, "spin_fragment_offset") == 7,
            QStringLiteral("碎片偏移 7 → 回填"));

    FilterNode lut(OpName::ColorLookup);
    lut.setColorLookupPreset(2);
    lut.setGradientMapStrength(40);
    host.setNode(lut);
    QWidget *lutPage = currentPage(host);
    r.check(sliderValue(lutPage, "slider_strength") == 40
                && spinValue(lutPage, "spin_strength") == 40,
            QStringLiteral("颜色查找强度 40 → 回填"));
    return r.fails;
}
