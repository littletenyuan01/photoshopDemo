#ifndef PIXMAPUTILS_H
#define PIXMAPUTILS_H

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRect>

/**
 * 位图 / 图标的公共工具（ui）。
 *
 * 【为什么集中到这里】「按 DPR 光栅化」与「透明棋盘格」这两件事原先在四个文件里
 * 各写了一份：`itemtreepanel.cpp`（svgIcon / paintCheckerboard）、
 * `colorspanel.cpp`（canvas / twoScaleIcon / paintChecker）、
 * `canvasview.cpp`（drawCheckerboard）、`toolbox.cpp`（toolIcon）。
 * 同一类 DPR 坑（见 `docs/tech-notes.md` 的「第二代 DPR 坑」）因此被重复踩过，
 * 三份棋盘格也只是配色/格子大小略有差异的同一种算法。故收敛于此。
 *
 * 【约定】手工绘制时一律用**逻辑坐标**，缩放交给 QPainter 处理；
 * 不要「先 setDevicePixelRatio 再按设备像素画」，那会画两遍（真实踩过的坑）。
 */
namespace PixmapUtils {

/** 逻辑尺寸 w×h、按 scale 倍光栅化的透明画布（已打好 DPR）。 */
inline QPixmap canvas(int w, int h, int scale)
{
    QPixmap pm(w * scale, h * scale);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(scale);
    return pm;
}

/**
 * 用同一段绘制代码产出 1..maxScale 各档图标（与 `ItemTreePanel::svgIcon` 同一思路）。
 * @param paint 形如 void(QPainter &, const QRect &logicalBox)
 */
template <typename Paint>
QIcon multiScaleIcon(int w, int h, int maxScale, Paint paint)
{
    QIcon icon;
    for (int scale = 1; scale <= maxScale; ++scale) {
        QPixmap pm = canvas(w, h, scale);
        QPainter painter(&pm);
        paint(painter, QRect(0, 0, w, h));
        painter.end();
        icon.addPixmap(pm);
    }
    return icon;
}

/**
 * 透明棋盘格（表现透明区）：cell 为逻辑边长，light/dark 交替。
 * 各调用点的格子大小与配色不同（缩略图 / 画布 / 渐变缩略图），故都由参数给。
 */
inline void paintChecker(QPainter &painter, const QRect &rect, int cell,
                         const QColor &light, const QColor &dark)
{
    for (int y = rect.top(); y < rect.bottom(); y += cell) {
        for (int x = rect.left(); x < rect.right(); x += cell) {
            const bool isLight = ((x / cell) + (y / cell)) % 2 == 0;
            painter.fillRect(QRect(x, y, cell, cell).intersected(rect), isLight ? light : dark);
        }
    }
}

} // namespace PixmapUtils

#endif // PIXMAPUTILS_H
