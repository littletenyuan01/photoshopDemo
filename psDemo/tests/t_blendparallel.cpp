/**
 * t_blendparallel.cpp — Compositor::blendLayerRangeParallel 与串行版逐像素一致。
 *
 * 这是移动工具「拖中逐帧」的路径：上方有调整层时每帧都要重跑上方全部层，
 * 串行版在真实文档（2480x3508 + 2 个调整层）上约 30ms/帧，改为分块并行后约 3.5ms/帧。
 * 正确性判据：从**同一张起始图**出发，串行与并行结果逐像素相同。
 * 耗时只打印、不断言（机器相关）。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "testutil.h"

#include <QElapsedTimer>

#include <algorithm>
#include <vector>

using namespace Ps;
using PsTests::Reporter;

namespace {

double medianOf(std::vector<double> &v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int tBlendParallel()
{
    Reporter r;

    const int W = 1600, H = 1200;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("L1"));
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 2, 200), QStringLiteral("L2"));
    doc->addBrightnessContrastFilter(0.18, 0.12); // 带滤镜层 → 走区域化栅格

    const int adj = doc->addAdjustmentLayer(OpName::Levels);
    {
        FilterNode n = doc->layers().layerAt(adj)->filters().at(0);
        n.setLevelsBlack(20.0);
        n.setLevelsWhite(230.0);
        n.setLevelsGamma(1.4);
        doc->setLayerFilterNode(adj, 0, n, false);
    }

    const QRect area(0, 0, W, H);
    const int layerCount = doc->layers().count();
    const QImage start = PsTests::makeLayerImage(W, H, 9, 120);

    QImage ser = start;
    Compositor::blendLayerRange(ser, *doc, area, 0, layerCount, -1);
    QImage par = start;
    bool ok = Compositor::blendLayerRangeParallel(par, *doc, area, 0, layerCount, -1);
    r.check(ok, QStringLiteral("blendLayerRangeParallel 返回成功"));
    const long long d = PsTests::diffCount(ser, par);
    r.check(d == 0, QStringLiteral("串行 vs 并行 逐像素一致（diffs=%1）").arg(d));

    // 耗时（信息，不作断言）
    std::vector<double> ts, tp;
    QImage work = start;
    for (int i = 0; i < 3; ++i) {
        QElapsedTimer t;
        t.start();
        Compositor::blendLayerRange(work, *doc, area, 0, layerCount, -1);
        ts.push_back(t.nsecsElapsed() / 1e6);

        t.restart();
        Compositor::blendLayerRangeParallel(work, *doc, area, 0, layerCount, -1);
        tp.push_back(t.nsecsElapsed() / 1e6);
    }
    const double ms = medianOf(ts), mp = medianOf(tp);
    r.info(QStringLiteral("%1x%2 全幅叠全部层：串行 %3 ms → 并行 %4 ms（%5x）")
               .arg(W)
               .arg(H)
               .arg(ms, 0, 'f', 1)
               .arg(mp, 0, 'f', 1)
               .arg(ms / (mp > 0.0 ? mp : 1e-9), 0, 'f', 1));
    return r.fails;
}
