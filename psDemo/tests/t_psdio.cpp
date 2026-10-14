/**
 * t_psdio.cpp — PSD 子集导出→导入往返：层属性 + 合成像素。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "engine/compositor.h"
#include "io/psdio.h"
#include "testutil.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QIODevice>

using namespace Ps;
using PsTests::Reporter;

int tPsdIo()
{
    Reporter r;

    const int W = 96, H = 64;
    auto doc = ImageDocument::createBlank(W, H, Qt::white);
    {
        ImageDocument::HistorySuppress suppress(*doc);
        // 再叠一层半透明、偏移、Multiply
        QImage overlay = PsTests::makeLayerImage(40, 30, 3, 200);
        auto layer = std::make_unique<Layer>(QStringLiteral("overlay"), overlay);
        layer->setOffsetSilent(12, 8);
        layer->setOpacity(0.6);
        layer->setBlendMode(BlendMode::Multiply);
        doc->addLayer(std::move(layer));
        doc->setActiveLayerIndex(1);
    }

    const QImage before = Compositor::composite(*doc);
    const QString path = PsTests::tempFilePath(QStringLiteral("roundtrip.psd"));

    QString err;
    r.check(PsdIo::save(*doc, path, &err),
            QStringLiteral("导出 PSD（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));

    auto back = PsdIo::load(path, &err);
    r.check(back != nullptr,
            QStringLiteral("导入 PSD（%1）").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    if (!back)
        return r.fails;

    r.check(back->width() == W && back->height() == H,
            QStringLiteral("画布尺寸 %1×%2").arg(back->width()).arg(back->height()));
    r.check(back->layers().count() == doc->layers().count(),
            QStringLiteral("层数一致（%1）").arg(back->layers().count()));

    if (back->layers().count() >= 2) {
        const Layer *A = doc->layers().layerAt(1);
        const Layer *B = back->layers().layerAt(1);
        r.check(B->name() == A->name(), QStringLiteral("层名「%1」").arg(B->name()));
        r.check(B->offsetX() == A->offsetX() && B->offsetY() == A->offsetY(),
                QStringLiteral("offset (%1,%2)")
                    .arg(B->offsetX())
                    .arg(B->offsetY()));
        r.check(B->blendMode() == A->blendMode(), QStringLiteral("混合模式"));
        r.check(qAbs(B->opacity() - A->opacity()) < 0.01,
                QStringLiteral("不透明度 %1").arg(B->opacity()));
        r.check(B->isVisible() == A->isVisible(), QStringLiteral("可见性"));
    }

    const QImage after = Compositor::composite(*back);
    // 解预乘↔预乘有量化，允许少量差异
    const long long d = PsTests::diffCount(before, after);
    const int maxDiff = PsTests::maxChannelDiff(before, after);
    r.check(d == 0 || maxDiff <= 1,
            QStringLiteral("合成往返 diffs=%1 maxCh=%2").arg(d).arg(maxDiff));

    // 非法路径
    auto bad = PsdIo::load(QStringLiteral("no-such-file.psd"), &err);
    r.check(bad == nullptr && !err.isEmpty(), QStringLiteral("缺文件失败有原因"));

    // A16：层矩形过大须拒载（改写合法 PSD 里的层 bottom/right）
    {
        auto tiny = ImageDocument::createBlank(16, 16, Qt::white);
        const QString badPath = PsTests::tempFilePath(QStringLiteral("a16-huge-layer.psd"));
        QFile::remove(badPath);
        QString e2;
        r.check(PsdIo::save(*tiny, badPath, &e2),
                QStringLiteral("A16：写出样本（%1）").arg(e2));

        QFile f(badPath);
        r.check(f.open(QIODevice::ReadWrite), QStringLiteral("A16：打开样本"));
        QByteArray all = f.readAll();
        // 层矩形：top,left,bottom,right 大端 int32 = 0,0,16,16
        QByteArray needle;
        {
            QDataStream ns(&needle, QIODevice::WriteOnly);
            ns.setByteOrder(QDataStream::BigEndian);
            ns << qint32(0) << qint32(0) << qint32(16) << qint32(16);
        }
        const int at = all.indexOf(needle);
        r.check(at >= 0, QStringLiteral("A16：找到层矩形字节"));
        if (at >= 0) {
            QBuffer buf(&all);
            buf.open(QIODevice::ReadWrite);
            buf.seek(at);
            QDataStream ws(&buf);
            ws.setByteOrder(QDataStream::BigEndian);
            // bottom/right → 0x7FFFFFFF ⇒ 宽高远超 30000
            ws << qint32(0) << qint32(0) << qint32(0x7FFFFFFF) << qint32(0x7FFFFFFF);
            buf.close();
            f.seek(0);
            f.resize(0);
            f.write(all);
            f.close();

            auto rejected = PsdIo::load(badPath, &e2);
            r.check(rejected == nullptr, QStringLiteral("A16：过大层矩形拒载"));
            r.check(e2.contains(QStringLiteral("矩形无效或过大")),
                    QStringLiteral("A16：错误含「矩形无效或过大」（%1）").arg(e2));
        }
        QFile::remove(badPath);
    }

    return r.fails;
}
