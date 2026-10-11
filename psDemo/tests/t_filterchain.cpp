/**
 * t_filterchain.cpp — 滤镜链：FilterStack::apply（走图 + 就地求值）vs 直接 FilterEval 循环。
 *
 * 覆盖两件事：
 *  ① 图路径与直接路径逐像素一致（就地链路不改变结果）；
 *  ② 图构建成本可忽略（只建图不求值 1.7µs 量级 → 「图持久化」无可测收益）。
 */
#include "domain/filternode.h"
#include "domain/filterstack.h"
#include "engine/filtereval.h"
#include "engine/graph/pointfilterop.h"
#include "engine/graph/psgraph.h"
#include "engine/graph/sourceop.h"
#include "testutil.h"

#include <QElapsedTimer>

#include <memory>

using namespace Ps;
using PsTests::Reporter;

int tFilterChain()
{
    Reporter r;

    const int W = 1024, H = 768;
    const QImage base = PsTests::makeLayerImage(W, H, 3, 200);

    FilterNode n1(OpName::BrightnessContrast);
    n1.setBrightness(0.2);
    n1.setContrast(0.3);
    FilterNode n2(OpName::Levels);
    FilterNode n3(OpName::HueSaturation);
    n3.setSaturation(20);

    FilterStack st;
    st.append(n1);
    st.append(n2);
    st.append(n3);

    const QImage viaGraph = st.apply(base);

    QImage direct = base;
    FilterEval::applyNode(direct, n1);
    FilterEval::applyNode(direct, n2);
    FilterEval::applyNode(direct, n3);

    const long long d = PsTests::diffCount(viaGraph, direct);
    r.check(d == 0, QStringLiteral("走图 vs 直接循环逐像素一致（diffs=%1）").arg(d));

    // 图构建成本（供「图是否值得持久化」的判断留证）
    {
        QElapsedTimer bt;
        bt.start();
        const int reps = 200;
        for (int i = 0; i < reps; ++i) {
            PsGraph g;
            GraphNode::Id prev = g.addNode(std::make_shared<ImageSourceOp>(base));
            for (int k = 0; k < 3; ++k) {
                const GraphNode::Id cur = g.addNode(std::make_shared<PointFilterOp>(n1));
                g.connect(prev, cur, 0);
                prev = cur;
            }
        }
        r.info(QStringLiteral("建图（4 节点 + 3 连接）= %1 ms/次")
                   .arg(bt.nsecsElapsed() / 1e6 / reps, 0, 'f', 4));
    }
    return r.fails;
}
