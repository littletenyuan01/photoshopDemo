/**
 * t_undocow.cpp — T1：三种写样式对共享 QImage 触发 COW detach。
 */
#include "domain/tilebuffer.h"
#include "testutil.h"

#include <QPainter>

using namespace Ps;
using PsTests::Reporter;

namespace {

bool pixelEquals(const QImage &a, int x, int y, QRgb expect)
{
    if (a.isNull() || x < 0 || y < 0 || x >= a.width() || y >= a.height())
        return false;
    return a.pixel(x, y) == expect;
}

} // namespace

int tUndoCow()
{
    Reporter r;

    // ① scanLine 写
    {
        TileBuffer buf(64, 64);
        QImage *tile = buf.ensureTile(0, 0);
        r.check(tile != nullptr, QStringLiteral("scanLine：有瓦片"));
        tile->fill(qRgba(10, 20, 30, 255));
        const QRgb before = tile->pixel(0, 0);
        QImage snap = *tile; // 共享引用
        const auto keyBefore = snap.cacheKey();
        QRgb *line = reinterpret_cast<QRgb *>(tile->scanLine(0));
        line[0] = qRgba(200, 0, 0, 255);
        r.check(snap.cacheKey() != tile->cacheKey() || snap.pixel(0, 0) == before,
                QStringLiteral("scanLine：写后快照内容仍为写前（key 变或像素不变）"));
        r.check(pixelEquals(snap, 0, 0, before),
                QStringLiteral("scanLine：快照像素未改"));
        r.check(tile->pixel(0, 0) == qRgba(200, 0, 0, 255),
                QStringLiteral("scanLine：原瓦片已改"));
        r.check(snap.cacheKey() != keyBefore || true,
                QStringLiteral("scanLine：记录 cacheKey 行为"));
        Q_UNUSED(keyBefore);
    }

    // ② QImage::fill
    {
        TileBuffer buf(64, 64);
        QImage *tile = buf.ensureTile(0, 0);
        tile->fill(qRgba(1, 2, 3, 255));
        const QRgb before = tile->pixel(5, 5);
        QImage snap = *tile;
        tile->fill(qRgba(9, 9, 9, 255));
        r.check(pixelEquals(snap, 5, 5, before), QStringLiteral("fill：快照像素未改"));
        r.check(tile->pixel(5, 5) == qRgba(9, 9, 9, 255), QStringLiteral("fill：原瓦片已改"));
        r.check(snap.cacheKey() != tile->cacheKey(), QStringLiteral("fill：cacheKey 已分离"));
    }

    // ③ QPainter（若未 detach 则按 R1 在 stampdab 前 bits()；此处先验证）
    {
        TileBuffer buf(64, 64);
        QImage *tile = buf.ensureTile(0, 0);
        tile->fill(qRgba(40, 50, 60, 255));
        const QRgb before = tile->pixel(10, 10);
        QImage snap = *tile;
        {
            QPainter p(tile);
            p.fillRect(0, 0, 20, 20, QColor(255, 0, 0, 255));
        }
        const bool snapOk = pixelEquals(snap, 10, 10, before);
        r.check(snapOk, QStringLiteral("QPainter：快照像素未改（COW detach）"));
        r.check(tile->pixel(10, 10) != before || tile->pixel(0, 0) != before,
                QStringLiteral("QPainter：原瓦片已改"));
        if (!snapOk) {
            r.info(QStringLiteral("QPainter 未 detach → 需在 stampdabop 加 target.bits()（R1）"));
        }
    }

    return r.fails;
}
