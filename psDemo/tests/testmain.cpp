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
int tFilterPanel();
int tFilterAsync();
int tFilterPreviewAsync();
int tPreviewDownsample();
int tRoi();
int tOffset();
int tMoreFilters();
int tHighPassMotion();
int tNoise();

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
    { "areafilter", "面积算子（盒模糊 / 高斯 / 中值 / 锐化 / 浮雕 / USM / 查找边缘）：ROI 外扩、分块无接缝、语义 oracle、精度量化、往返", tAreaFilter },
    { "filterundo", "滤镜对话框的撤销原子性：确定=一条撤销、取消=不留痕", tFilterUndo },
    { "filterpanel", "图层滤镜栈面板：列出/开关/换序/删除都真的驱动 domain，且各留一条 undo", tFilterPanel },
    { "filterasync", "滤镜定稿的异步/可取消链路：协作式取消、区域缓存命中且与同步一致、世代号失效", tFilterAsync },
    { "filterpreview", "滤镜预览的异步化：静默标脏不发 contentChanged、后台只算给定区域、结果与整层裁块一致", tFilterPreviewAsync },
    { "previewdown", "预览降采样：空间参数按倍数缩小（0 保持 0、钳到 1）、任务照常写缓存、结果与原分辨率大体相近", tPreviewDownsample },
    { "roictx", "ROI 第①步：两条求值路径都按坐标空间填 src/out 几何，且退回本地坐标时结果不变", tRoi },
    { "offset", "位移（几何算子）：wrap/透明语义 oracle、分块投影无块界错位、全幅求值护栏、往返", tOffset },
    { "morefilters", "最大值/最小值/自定滤镜：语义 oracle、分块投影无接缝、参数往返", tMoreFilters },
    { "hpblur", "高通保留/运动模糊：组合式与一维盒 oracle、分块投影无接缝、往返", tHighPassMotion },
    { "noise", "添加杂色：确定性（同参可复现）、位置稳定（区域==整层裁块）、单色/高斯、往返", tNoise },
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
