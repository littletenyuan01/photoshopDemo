/**
 * t_adjarea.cpp — 调整层 + 面积算子：子缓冲外扩 → 分块投影 == 整幅。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "engine/op/opname.h"
#include "engine/projection.h"
#include "io/projectio.h"
#include "testutil.h"

using namespace Ps;
using PsTests::Reporter;

namespace {

QImage chunkedProjection(ImageDocument *doc)
{
    Projection p;
    p.bind(doc);
    p.setPriorityRect(QRect(0, 0, doc->width(), doc->height()));
    doc->markDirty();
    p.sync();
    while (p.hasPendingChunks())
        p.processPendingChunks(0);
    return p.image();
}

} // namespace

int tAdjArea()
{
    Reporter r;

    r.check(isAdjustmentOp(OpName::BoxBlur), QStringLiteral("BoxBlur 可作为调整层"));
    r.check(isAdjustmentOp(OpName::GaussianBlur), QStringLiteral("GaussianBlur 可作为调整层"));
    r.check(isAdjustmentOp(OpName::FindEdges), QStringLiteral("FindEdges 可作为调整层"));
    r.check(isAdjustmentOp(OpName::SurfaceBlur), QStringLiteral("SurfaceBlur 可作为调整层"));
    r.check(!isAdjustmentOp(OpName::Offset), QStringLiteral("Offset 仍非调整层"));
    r.check(!isAdjustmentOp(OpName::AddNoise), QStringLiteral("AddNoise 仍非调整层"));
    r.check(!isAdjustmentOp(OpName::Tiles), QStringLiteral("Tiles 仍非调整层"));

    {
        auto doc = ImageDocument::createBlank(256, 192, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(240, 180, 41, 255), QStringLiteral("px"));
        doc->layers().layerAt(1)->setOffset(8, 6);
        const int adj = doc->addAdjustmentLayer(OpName::BoxBlur);
        r.check(adj >= 0, QStringLiteral("新建盒模糊调整层"));
        if (adj >= 0) {
            FilterNode n = doc->layers().layerAt(adj)->filters().at(0);
            n.setBlurRadius(6);
            doc->setLayerFilterNode(adj, 0, n, false);
        }
        const QImage whole = Compositor::composite(*doc);
        const long long dc = PsTests::diffCount(whole, chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("盒模糊调整层：分块投影 == 整幅（diffs=%1）").arg(dc));
        auto bare = ImageDocument::createBlank(256, 192, Qt::white);
        bare->placeImageAsLayer(PsTests::makeLayerImage(240, 180, 41, 255), QStringLiteral("px"));
        bare->layers().layerAt(1)->setOffset(8, 6);
        const long long changed = PsTests::diffCount(whole, Compositor::composite(*bare));
        r.check(changed > 0, QStringLiteral("盒模糊调整层确实改变合成（diffs=%1）").arg(changed));
    }

    {
        auto doc = ImageDocument::createBlank(160, 120, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(160, 120, 7, 255), QStringLiteral("px"));
        const int adj = doc->addAdjustmentLayer(OpName::FindEdges);
        r.check(adj >= 0, QStringLiteral("新建查找边缘调整层"));
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("查找边缘调整层：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(128, 96, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(128, 96, 13, 255), QStringLiteral("px"));
        const int adj = doc->addAdjustmentLayer(OpName::MotionBlur);
        r.check(adj >= 0, QStringLiteral("新建运动模糊调整层"));
        if (adj >= 0) {
            FilterNode n = doc->layers().layerAt(adj)->filters().at(0);
            n.setMotionDistance(24);
            n.setMotionAngle(0);
            doc->setLayerFilterNode(adj, 0, n, false);
        }
        const long long dc =
            PsTests::diffCount(Compositor::composite(*doc), chunkedProjection(doc.get()));
        r.check(dc == 0, QStringLiteral("运动模糊调整层：分块投影 == 整幅（diffs=%1）").arg(dc));
    }

    {
        auto doc = ImageDocument::createBlank(96, 72, Qt::white);
        doc->placeImageAsLayer(PsTests::makeLayerImage(96, 72, 19, 255), QStringLiteral("px"));
        const int adj = doc->addAdjustmentLayer(OpName::BoxBlur);
        r.check(adj >= 0, QStringLiteral("往返：新建盒模糊调整层"));
        if (adj >= 0) {
            FilterNode n = doc->layers().layerAt(adj)->filters().at(0);
            n.setBlurRadius(7);
            doc->setLayerFilterNode(adj, 0, n, false);
        }
        const QString path = PsTests::tempFilePath(QStringLiteral("adjarea.pslite"));
        QString err;
        const bool saved = ProjectIo::save(*doc, path, &err);
        auto back = saved ? ProjectIo::load(path, &err) : nullptr;
        if (back) {
            bool found = false;
            for (int i = 0; i < back->layers().count(); ++i) {
                Layer *L = back->layers().layerAt(i);
                if (L->isAdjustmentLayer() && L->filters().count() > 0
                    && L->filters().at(0).op() == OpName::BoxBlur
                    && L->filters().at(0).blurRadius() == 7) {
                    found = true;
                }
            }
            r.check(found, QStringLiteral("盒模糊调整层：参数往返"));
        } else {
            r.check(false, QStringLiteral("重新打开含盒模糊调整层的 .pslite"));
        }
    }

    return r.fails;
}
