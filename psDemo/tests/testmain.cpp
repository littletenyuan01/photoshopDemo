/**
 * testmain.cpp — 常驻测试入口与用例注册表。
 *
 * 无参数跑全部；给用例名只跑指定用例。退出码 = 失败用例数（便于脚本/CI 判定）。
 */
#include "engine/op/opsinit.h"
#include "testutil.h"

#include <QApplication>
#include <QStringList>
#include <QtGlobal>

#include <cstdio>

using PsTests::Case;

// —— 各用例实现（见 t_*.cpp），返回失败项数 ——
int tPixelPrim();
int tFilterChain();
int tRegionRaster();
int tComposite();
int tGraphCheck();
int tProjectIo();
int tAdjustPanel();
int tAdjustApply();
int tBlendParallel();
int tTransformLive();
int tDirtyBudget();
int tAreaFilter();
int tFilterUndo();

namespace {

const Case kCases[] = {
    { "pixelprim", "逐像素原语：新实现 vs 改动前旧实现，穷举位等价", tPixelPrim },
    { "filterchain", "滤镜链走图 vs 直接 FilterEval，逐像素一致", tFilterChain },
    { "regionraster", "层复合栅格区域化 vs 整层栅格，逐像素一致", tRegionRaster },
    { "composite", "投影分块并行合成 vs 单次整体合成，逐像素一致", tComposite },
    { "graphcheck", "图引擎自检（结构/缓存/脏传播/合成交叉校验）", tGraphCheck },
    { "projectio", ".pslite 存读往返：层/调整层参数/像素全保真", tProjectIo },
    { "adjustpanel", "调整层属性面板：参数回填 + 预览/提交链路", tAdjustPanel },
    { "adjustapply", "调整层参数落盘后重新打开仍作用于画面", tAdjustApply },
    { "blendparallel", "blendLayerRangeParallel 与串行版逐像素一致", tBlendParallel },
    { "transformlive", "自由变换 live 投影与提交后合成一致", tTransformLive },
    { "dirtybudget", "带滤镜层的小脏区重投影受时间预算约束（回归护栏）", tDirtyBudget },
    { "areafilter", "面积算子（盒模糊 / 高斯 / 中值 / USM / 查找边缘）：ROI 外扩、分块无接缝、语义 oracle、精度量化、往返", tAreaFilter },
    { "filterundo", "滤镜对话框的撤销原子性：确定=一条撤销、取消=不留痕", tFilterUndo },
};

} // namespace

int main(int argc, char **argv)
{
    // 面板类用例需要 QApplication，但不需要真窗口/真渲染：未显式指定时用 offscreen，
    // 保证在任意 shell（含无桌面环境）下都能跑。
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    Ps::opsInit();

    const QStringList want = app.arguments().mid(1);
    const int total = int(sizeof(kCases) / sizeof(kCases[0]));
    int ran = 0, failed = 0;
    for (int i = 0; i < total; ++i) {
        const Case &c = kCases[i];
        if (!want.isEmpty() && !want.contains(QString::fromLatin1(c.name)))
            continue;
        ++ran;
        std::printf("==== %s — %s ====\n", c.name, c.desc);
        const int f = c.fn();
        std::printf("---- %s: %s\n\n", c.name, f == 0 ? "OK" : "FAILED");
        if (f != 0)
            ++failed;
    }

    if (ran == 0) {
        std::printf("没有匹配的用例。可用：");
        for (int i = 0; i < total; ++i)
            std::printf("%s ", kCases[i].name);
        std::printf("\n");
        return 2;
    }
    std::printf("======== %d/%d 通过 ========\n", ran - failed, ran);
    return failed;
}
