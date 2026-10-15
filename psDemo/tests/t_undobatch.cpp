/**
 * t_undobatch.cpp — T1：ChangeBatch 合并变更类信号。
 */
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "testutil.h"

#include <QObject>
#include <QRect>
#include <QVector>

using namespace Ps;
using PsTests::Reporter;

int tUndoBatch()
{
    Reporter r;

    // —— 批量内：10 次 markDirty + 2 次增删层 → 各信号各 1 次；pixels 为并集 ——
    {
        auto doc = ImageDocument::createBlank(256, 256, Qt::white);
        int contentN = 0, structureN = 0, pixelsN = 0;
        QRect pixelsUnion;
        QObject::connect(doc.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        QObject::connect(doc.get(), &ImageDocument::structureChanged, [&]() { ++structureN; });
        QObject::connect(doc.get(), &ImageDocument::pixelsChanged, [&](const QRect &rect) {
            ++pixelsN;
            pixelsUnion = pixelsUnion.isNull() ? rect : pixelsUnion.united(rect);
        });

        QRect expectUnion;
        {
            ImageDocument::ChangeBatch batch(*doc);
            for (int i = 0; i < 10; ++i) {
                const QRect rect(10 + i * 5, 20 + i * 3, 30, 24);
                expectUnion = expectUnion.isNull() ? rect : expectUnion.united(rect);
                doc->markDirty(rect);
            }
            doc->addTransparentLayer(QStringLiteral("L1"));
            doc->addTransparentLayer(QStringLiteral("L2"));
            r.check(contentN == 0 && structureN == 0 && pixelsN == 0,
                    QStringLiteral("批量内未提前发射（c=%1 s=%2 p=%3）")
                        .arg(contentN)
                        .arg(structureN)
                        .arg(pixelsN));
        }

        r.check(contentN == 1, QStringLiteral("批量后 contentChanged==1（%1）").arg(contentN));
        r.check(structureN == 1, QStringLiteral("批量后 structureChanged==1（%1）").arg(structureN));
        r.check(pixelsN == 1, QStringLiteral("批量后 pixelsChanged==1（%1）").arg(pixelsN));
        r.check(pixelsUnion == expectUnion,
                QStringLiteral("pixelsChanged 并集=%1,%2 %3×%4 期望=%5,%6 %7×%8")
                    .arg(pixelsUnion.x())
                    .arg(pixelsUnion.y())
                    .arg(pixelsUnion.width())
                    .arg(pixelsUnion.height())
                    .arg(expectUnion.x())
                    .arg(expectUnion.y())
                    .arg(expectUnion.width())
                    .arg(expectUnion.height()));
        r.info(QStringLiteral("批量内 10 dirty + 2 增层：content=%1 structure=%2 pixels=%3")
                   .arg(contentN)
                   .arg(structureN)
                   .arg(pixelsN));
    }

    // —— 批量外：行为与改造前一致（每次 markDirty 各发一次）——
    {
        auto doc = ImageDocument::createBlank(128, 128, Qt::white);
        int contentN = 0, pixelsN = 0;
        QObject::connect(doc.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        QObject::connect(doc.get(), &ImageDocument::pixelsChanged, [&](const QRect &) { ++pixelsN; });

        doc->markDirty(QRect(0, 0, 10, 10));
        doc->markDirty(QRect(20, 20, 10, 10));
        doc->markDirty(QRect(40, 40, 10, 10));
        r.check(contentN == 3 && pixelsN == 3,
                QStringLiteral("批量外 3 次 markDirty → content=%1 pixels=%2（期望 3/3）")
                    .arg(contentN)
                    .arg(pixelsN));
    }

    // —— 嵌套 ChangeBatch：仅最外层析构补发 ——
    {
        auto doc = ImageDocument::createBlank(64, 64, Qt::white);
        int contentN = 0;
        QObject::connect(doc.get(), &ImageDocument::contentChanged, [&]() { ++contentN; });
        {
            ImageDocument::ChangeBatch outer(*doc);
            {
                ImageDocument::ChangeBatch inner(*doc);
                doc->markDirty(QRect(1, 1, 8, 8));
                r.check(contentN == 0, QStringLiteral("内层析构前不发"));
            }
            r.check(contentN == 0, QStringLiteral("内层析构后仍不发（外层未结束）"));
            doc->markDirty(QRect(10, 10, 8, 8));
        }
        r.check(contentN == 1, QStringLiteral("嵌套最外层析构 contentChanged==1（%1）").arg(contentN));
    }

    return r.fails;
}
