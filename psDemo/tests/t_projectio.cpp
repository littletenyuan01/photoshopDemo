/**
 * t_projectio.cpp — .pslite 存读往返：层结构 / 调整层参数 / 像素 全保真。
 *
 * 除了逐字段比对，最后用「整幅合成逐像素一致」做端到端判据 ——
 * 它同时覆盖像素数据、滤镜链、调整层参数、蒙版、层属性与 offset。
 */
#include "domain/filternode.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/path.h"
#include "engine/compositor.h"
#include "io/projectio.h"
#include "testutil.h"

#include <QDataStream>
#include <QFile>
#include <QPointF>

using namespace Ps;
using PsTests::Reporter;

namespace {

PathAnchor anchorAt(const QPointF &p, const QPointF &inH, const QPointF &outH, bool corner)
{
    PathAnchor a;
    a.point = p;
    a.inHandle = inH;
    a.outHandle = outH;
    a.inExplicit = (inH != p);
    a.outExplicit = (outH != p);
    a.corner = corner;
    return a;
}

bool anchorsEqual(const PathAnchor &a, const PathAnchor &b)
{
    return a.point == b.point && a.inHandle == b.inHandle && a.outHandle == b.outHandle
           && a.inExplicit == b.inExplicit && a.outExplicit == b.outExplicit
           && a.corner == b.corner;
}

} // namespace

int tProjectIo()
{
    Reporter r;

    const int W = 320, H = 240;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    doc->placeImageAsLayer(PsTests::makeLayerImage(W, H, 1, 255), QStringLiteral("bg"));
    doc->addBrightnessContrastFilter(0.2, 0.1); // 像素层滤镜

    // 调整层 1：亮度/对比度（非默认值）
    const int a1 = doc->addAdjustmentLayer(OpName::BrightnessContrast);
    {
        FilterNode n = doc->layers().layerAt(a1)->filters().at(0);
        n.setBrightness(0.42);
        n.setContrast(-0.17);
        doc->setLayerFilterNode(a1, 0, n, false);
    }
    // 调整层 2：色阶（含容易被量程夹住的 gamma）
    const int a2 = doc->addAdjustmentLayer(OpName::Levels);
    {
        FilterNode n = doc->layers().layerAt(a2)->filters().at(0);
        n.setLevelsBlack(137.0);
        n.setLevelsWhite(168.0);
        n.setLevelsGamma(4.79);
        doc->setLayerFilterNode(a2, 0, n, false);
    }
    // 层属性 + 蒙版 + offset
    doc->setLayerOpacity(1, 0.6);
    doc->setLayerBlendMode(1, BlendMode::Multiply);
    doc->translateLayer(1, 7, -5, /*emitContent=*/false);
    doc->addLayerMask(1, ImageDocument::LayerMaskInit::HideAll);

    const QImage before = Compositor::composite(*doc);

    const QString path = PsTests::tempFilePath(QStringLiteral("roundtrip.pslite"));
    QString err;
    r.check(ProjectIo::save(*doc, path, &err),
            QStringLiteral("保存 .pslite（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    auto back = ProjectIo::load(path, &err);
    r.check(back != nullptr,
            QStringLiteral("重新打开 .pslite（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    if (!back)
        return r.fails;

    r.check(back->layers().count() == doc->layers().count(),
            QStringLiteral("层数一致（%1）").arg(back->layers().count()));

    int fieldBad = 0;
    for (int i = 0; i < doc->layers().count() && i < back->layers().count(); ++i) {
        Layer *A = doc->layers().layerAt(i);
        Layer *B = back->layers().layerAt(i);
        if (A->isAdjustmentLayer() != B->isAdjustmentLayer())
            ++fieldBad;
        if (A->opacity() != B->opacity() || A->blendMode() != B->blendMode()
            || A->offsetX() != B->offsetX() || A->offsetY() != B->offsetY()
            || A->hasMask() != B->hasMask())
            ++fieldBad;
        if (A->filters().count() != B->filters().count()) {
            ++fieldBad;
            continue;
        }
        for (int k = 0; k < A->filters().count(); ++k) {
            const FilterNode &x = A->filters().at(k);
            const FilterNode &y = B->filters().at(k);
            if (x.op() != y.op() || x.isEnabled() != y.isEnabled()
                || x.brightness() != y.brightness() || x.contrast() != y.contrast()
                || x.levelsBlack() != y.levelsBlack() || x.levelsWhite() != y.levelsWhite()
                || x.levelsGamma() != y.levelsGamma() || x.saturation() != y.saturation())
                ++fieldBad;
        }
    }
    r.check(fieldBad == 0,
            QStringLiteral("层 kind / 属性 / 滤镜参数逐项一致（不一致 %1 项）").arg(fieldBad));

    const QImage after = Compositor::composite(*back);
    const long long d = PsTests::diffCount(before, after);
    r.check(d == 0, QStringLiteral("存读往返后整幅合成逐像素一致（diffs=%1）").arg(d));

    // —— 路径往返：3 条（闭合 / 多子路径 / 显式柄+尖角）——
    {
        auto docP = ImageDocument::createBlank(80, 60, Qt::white);
        {
            Path p;
            p.name = QStringLiteral("closed");
            SubPath sp;
            sp.closed = true;
            sp.anchors << anchorAt({10, 10}, {10, 10}, {20, 5}, false)
                       << anchorAt({50, 10}, {40, 5}, {50, 10}, false)
                       << anchorAt({30, 40}, {30, 40}, {30, 40}, true);
            p.subPaths.append(sp);
            docP->addPath(p);
        }
        {
            Path p;
            p.name = QStringLiteral("multi");
            SubPath outer;
            outer.closed = true;
            outer.anchors << anchorAt({5, 5}, {5, 5}, {5, 5}, false)
                          << anchorAt({70, 5}, {70, 5}, {70, 5}, false)
                          << anchorAt({70, 50}, {70, 50}, {70, 50}, false)
                          << anchorAt({5, 50}, {5, 50}, {5, 50}, false);
            SubPath hole;
            hole.closed = true;
            hole.anchors << anchorAt({25, 20}, {25, 20}, {25, 20}, false)
                         << anchorAt({45, 20}, {45, 20}, {45, 20}, false)
                         << anchorAt({35, 35}, {35, 35}, {35, 35}, false);
            p.subPaths << outer << hole;
            docP->addPath(p);
        }
        {
            Path p;
            p.name = QStringLiteral("handles");
            p.visible = false;
            SubPath sp;
            sp.closed = false;
            sp.anchors << anchorAt({0, 0}, {0, 0}, {12, -8}, false)
                       << anchorAt({40, 20}, {28, 10}, {40, 20}, true);
            p.subPaths.append(sp);
            docP->addPath(p);
        }

        const QString pPath = PsTests::tempFilePath(QStringLiteral("paths-roundtrip.pslite"));
        QString perr;
        r.check(ProjectIo::save(*docP, pPath, &perr),
                QStringLiteral("保存含路径工程（%1）").arg(perr));
        auto backP = ProjectIo::load(pPath, &perr);
        r.check(backP != nullptr, QStringLiteral("读回含路径工程（%1）").arg(perr));
        if (backP) {
            r.check(backP->paths().count() == 3, QStringLiteral("路径数=3"));
            int bad = 0;
            for (int i = 0; i < 3; ++i) {
                const Path *A = docP->paths().pathAt(i);
                const Path *B = backP->paths().pathAt(i);
                if (!A || !B || A->name != B->name || A->visible != B->visible
                    || A->subPaths.size() != B->subPaths.size()) {
                    ++bad;
                    continue;
                }
                for (int s = 0; s < A->subPaths.size(); ++s) {
                    if (A->subPaths[s].closed != B->subPaths[s].closed
                        || A->subPaths[s].anchors.size() != B->subPaths[s].anchors.size()) {
                        ++bad;
                        continue;
                    }
                    for (int k = 0; k < A->subPaths[s].anchors.size(); ++k) {
                        if (!anchorsEqual(A->subPaths[s].anchors[k], B->subPaths[s].anchors[k]))
                            ++bad;
                    }
                }
            }
            r.check(bad == 0, QStringLiteral("路径锚点浮点/标志逐位相等（坏 %1）").arg(bad));
        }
    }

    // —— 政策护栏：v3（旧格式）必须拒绝 ——
    {
        const QString oldPath = PsTests::tempFilePath(QStringLiteral("v3-reject.pslite"));
        QFile f(oldPath);
        r.check(f.open(QIODevice::WriteOnly), QStringLiteral("写 v3 样本"));
        if (f.isOpen()) {
            QDataStream out(&f);
            out.setVersion(QDataStream::Qt_6_0);
            out << ProjectIo::kMagic << quint32(3); // 旧 Current
            f.close();
            QString err;
            auto rejected = ProjectIo::load(oldPath, &err);
            r.check(rejected == nullptr, QStringLiteral("v3 样本 load 失败"));
            r.check(err.contains(QStringLiteral("格式不匹配")),
                    QStringLiteral("错误信息含格式不匹配（%1）").arg(err));
        }
    }

    return r.fails;
}
