/**
 * paintclip.h — 选区/ROI 窗口与 dab 回滚助手（engine/op 层）。
 *
 * operationWindow / roiWindow 限制缓冲算子遍历范围；restoreOutsideSelection 供 dab 用。
 * 避免 materialize 整层 + setFromImage 破坏 TileBuffer 稀疏设计。
 */
#ifndef ENGINE_OP_PAINTCLIP_H
#define ENGINE_OP_PAINTCLIP_H

#include "engine/paintselectionclip.h"
#include "domain/selection.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRgb>

namespace Ps {
namespace OpPaintClip {

/** 选区指针有效且 bounds 非空。 */
inline bool clipActive(const PaintSelectionClip &clip)
{
    return clip.selection && !clip.selection->bounds().isEmpty();
}

/** 层内坐标 (layerX, layerY) 是否在选区内；无选区恒 true。 */
inline bool layerPixelSelected(const PaintSelectionClip &clip, int layerX, int layerY)
{
    if (!clipActive(clip))
        return true;
    return clip.selection->isSelected(layerX + clip.layerOffsetX,
                                      layerY + clip.layerOffsetY);
}

/** 选区在层内坐标下的外接矩形（已与层范围求交）；无选区返回空矩形。 */
inline QRect selectionRectInLayer(const PaintSelectionClip &clip, int layerWidth, int layerHeight)
{
    if (!clipActive(clip))
        return {};
    return clip.selection->bounds()
        .translated(-clip.layerOffsetX, -clip.layerOffsetY)
        .intersected(QRect(0, 0, layerWidth, layerHeight));
}

/** 层范围与 roi 的交集（roi 为空 = 整层）。 */
inline QRect roiWindow(const QRect &roi, int layerWidth, int layerHeight)
{
    const QRect layerRect(0, 0, layerWidth, layerHeight);
    return (roi.isEmpty() ? layerRect : roi).intersected(layerRect);
}

/**
 * 算子工作窗口 = roiWindow ∩ 选区外接框（层内坐标）。返回空矩形 = 无事可做。
 *
 * 【用途】把缓冲算子的遍历与临时缓冲限制在「可能被改动的瓦片」上，
 * 而不是 `materialize()` 整层再 `setFromImage()` 整层回写
 * ——后者会把 TileBuffer 的稀疏瓦片设计（见 domain/tilebuffer.h）整块废掉。
 *
 * 【限定】只适用于**逐像素独立求值**的算子（实色填充 / 渐变 / dab）。
 * 传播类算子（洪泛）不要用它做计算窗口：洪水必须在整层上蔓延再按选区裁，
 * 否则绕过障碍的路径会被外接框截断。那种场合用 roiWindow()。
 */
inline QRect operationWindow(const PaintSelectionClip &clip,
                             const QRect &roi,
                             int layerWidth,
                             int layerHeight)
{
    const QRect win = roiWindow(roi, layerWidth, layerHeight);
    if (win.isEmpty() || !clipActive(clip))
        return win;
    return win.intersected(selectionRectInLayer(clip, layerWidth, layerHeight));
}

/**
 * 把 after 里某一块子区域内、选区之外的像素回滚为 before 的值。
 *
 * 【为什么要限定子区域】算子只改了自己绘制的那一小块，所以只需备份 / 回滚这一块
 * （dab 包围盒），而不是整块 64×64 瓦片——旧实现每个 dab 都要深拷一整块瓦片，
 * 再对整块跑 4096 次选区判定。
 *
 * 【两个坐标系】备份图 before 通常是从瓦片上裁下来的，所以它在 after 内的位置
 * （瓦片内坐标）与它在层内的位置（层内坐标）不是同一个值，须分别传入：
 * 前者决定往哪里写，后者决定选区判定取哪个像素。
 *
 * @param before        该子区域的备份，尺寸即区域尺寸（未做有效性判定的空图直接返回）
 * @param regionInImage 子区域左上角在 after 中的位置（瓦片内坐标）
 * @param regionInLayer 同一像素的层内坐标
 */
inline void restoreOutsideSelection(QImage &after,
                                    const QImage &before,
                                    const QPoint &regionInImage,
                                    const QPoint &regionInLayer,
                                    const PaintSelectionClip &clip)
{
    if (!clipActive(clip) || after.isNull() || before.isNull())
        return;
    if (before.width() <= 0 || before.height() <= 0)
        return;
    if (!after.rect().contains(QRect(regionInImage, before.size())))
        return;

    const int w = before.width();
    const int h = before.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(before.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(after.scanLine(regionInImage.y() + y))
                    + regionInImage.x();
        for (int x = 0; x < w; ++x) {
            if (!layerPixelSelected(clip, regionInLayer.x() + x, regionInLayer.y() + y))
                dst[x] = src[x];
        }
    }
}

} // namespace OpPaintClip
} // namespace Ps

#endif // ENGINE_OP_PAINTCLIP_H
