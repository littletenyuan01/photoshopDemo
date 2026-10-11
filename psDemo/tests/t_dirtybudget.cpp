/**
 * t_dirtybudget.cpp — 回归护栏：带滤镜的层上「画一笔」不得再触发无界停顿。
 *
 * 背景：markDirty 会清掉活动层的整层复合栅格；区域化之前，带 3 个点滤镜的层
 * 在 2000x1500 上重投影一次要 447ms（整层 materialize + 整条滤镜链），
 * 且完全不受投影时间预算约束 —— 表现为「在带滤镜的层上涂抹一动就卡」。
 * 区域化之后同一场景约 9ms。
 *
 * 这里刻意用「时间上界」而非精确值：阈值取 150ms，对当前实现有 ~16 倍余量，
 * 对区域化前的实现（此处约 280ms）则明显越界。机器相关，故阈值放得很宽。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/op/opsinit.h"
#include "engine/projection.h"
#include "testutil.h"

#include <QElapsedTimer>
#include <QThreadPool>

using namespace Ps;
using PsTests::Reporter;

int tDirtyBudget()
{
    Reporter r;

    const int W = 1600, H = 1200;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("L"));

    // 给该层挂 3 个点滤镜（markDirty 只清**活动层**的复合栅格，故它必须是活动层）
    Layer *layer = doc->layers().layerAt(1);
    FilterNode n1(OpName::BrightnessContrast);
    n1.setBrightness(0.2);
    n1.setContrast(0.3);
    FilterNode n2(OpName::Levels);
    FilterNode n3(OpName::HueSaturation);
    n3.setSaturation(20);
    layer->filters().append(n1);
    layer->filters().append(n2);
    layer->filters().append(n3);
    layer->invalidateCompositeRaster();
    doc->setActiveLayerIndex(1);
    r.check(doc->activeLayerIndex() == 1, QStringLiteral("带滤镜层是活动层（markDirty 会清它的栅格）"));

    const int threads = int(QThreadPool::globalInstance()->maxThreadCount());

    Projection p;
    p.bind(doc.get());
    p.setPriorityRect(QRect(0, 0, 512, 512));

    // 先整幅跑一遍，让栅格/投影进入稳定态（首次包含一次性分配与 paging）
    {
        doc->markDirty();
        p.sync();
        while (p.hasPendingChunks())
            p.processPendingChunks(0);
    }

    // 关键：只标 64x64 小脏区（≈ 一个画笔 dab），预算 8ms
    doc->markDirty(QRect(100, 100, 64, 64));
    QElapsedTimer t;
    t.start();
    p.sync(8);
    const double ms = t.nsecsElapsed() / 1e6;

    r.info(QStringLiteral("带 3 滤镜层上 64x64 脏区 sync(8ms) = %1 ms（%2 线程；区域化前约 280ms）")
               .arg(ms, 0, 'f', 1)
               .arg(threads));
    r.check(ms < 150.0, QStringLiteral("小脏区重投影未出现无界停顿（< 150ms 护栏）"));
    return r.fails;
}
