/**
 * t_graphcheck.cpp — 图引擎自检：结构校验 + 缓存复用 + 脏传播 + 与直接合成交叉校验。
 *
 * 实现见 engine/graph/graphselfcheck.cpp；这里只作为常驻回归入口。
 */
#include "engine/graph/graphselfcheck.h"
#include "testutil.h"

using PsTests::Reporter;

int tGraphCheck()
{
    Reporter r;
    r.check(Ps::graphSelfCheck(), QStringLiteral("graphSelfCheck() 全量通过"));
    return r.fails;
}
