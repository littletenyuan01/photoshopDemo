/**
 * t_transform.cpp — 自由变换的 live 投影与提交后合成一致（无 GUI）。
 *
 * ① identity 变换：live 必须与提交后的合成**逐像素相同**；
 * ② 缩小 50%（Bilinear）：live 与提交后应基本一致（允许极小偏差，给千分之一上限）。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/op/opsinit.h"
#include "engine/projection.h"
#include "testutil.h"
#include "tools/transformtool.h"

#include <QColor>

using namespace Ps;
using PsTests::Reporter;

namespace {

struct StubViewPort : ViewPort {
    void zoomAt(const QPointF &, qreal) override {}
    void panBy(const QPointF &) override {}
    void requestRepaint() override {}
};

long long diffCount(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    long long d = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y))
                ++d;
        }
    }
    return d;
}

} // namespace

int tTransformLive()
{
    Reporter r;

    const int W = 800, H = 600;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    QImage red(W, H, QImage::Format_ARGB32_Premultiplied);
    red.fill(qRgba(200, 40, 40, 255));
    doc->placeImageAsLayer(red, QStringLiteral("top"));

    Projection proj;
    proj.bind(doc.get());
    proj.setPriorityRect(QRect(0, 0, W, H));
    doc->markDirty();
    proj.sync();
    while (proj.hasPendingChunks())
        proj.processPendingChunks(0);
    const QImage snapshot = proj.image();

    const QImage before = Compositor::composite(*doc);

    ToolContext ctx;
    ctx.document = doc.get();
    ctx.projectionSnapshot = &snapshot;
    ctx.visibleDocRect = QRect(0, 0, W, H);

    // ① identity
    TransformTool tool;
    StubViewPort vp;
    tool.activate(ctx, vp);
    const QImage *live = tool.liveProjection();
    r.check(live != nullptr, QStringLiteral("激活后能拿到 live 投影"));
    if (!live)
        return r.fails;
    const long long dLive = diffCount(*live, before);
    r.check(dLive == 0, QStringLiteral("identity：live == 提交前合成（diffs=%1）").arg(dLive));

    tool.commitFromUi(ctx);
    const QImage afterCommit = Compositor::composite(*doc);
    const long long dAfter = diffCount(afterCommit, before);
    r.check(dAfter == 0, QStringLiteral("identity：提交后合成不变（diffs=%1）").arg(dAfter));

    // ② 缩小 50%（Bilinear）：live 与提交后应基本一致
    TransformTool tool2;
    tool2.setInterpolation(TransformInterpolation::Bilinear);
    tool2.activate(ctx, vp);
    tool2.applyNumericParams(0, 0, 50, 50, 0, 0, 0);
    const QImage *live2 = tool2.liveProjection();
    r.check(live2 != nullptr, QStringLiteral("缩放会话能拿到 live 投影"));
    if (!live2)
        return r.fails;
    const QImage liveCopy = live2->copy();
    tool2.commitFromUi(ctx);
    const QImage afterScale = Compositor::composite(*doc);
    const long long dScale = diffCount(liveCopy, afterScale);
    r.info(QStringLiteral("scale50：live vs 提交后 diffs=%1（%2%）")
               .arg(dScale)
               .arg(100.0 * double(dScale) / double(W * H), 0, 'f', 3));
    r.check(dScale >= 0 && dScale <= (W * H) / 1000,
            QStringLiteral("scale50：live 与提交后偏差在千分之一以内"));
    return r.fails;
}
