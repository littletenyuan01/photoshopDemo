/**
 * selectiontopath.h — 选区 mask → Path（engine 层）。
 *
 * 【算法】二值化 → Moore 邻域轮廓跟踪 → Douglas–Peucker 简化 → Catmull-Rom 控制柄。
 * 对照 pen-tool-plan §6.4；容差固定 1.0 文档像素。
 */
#ifndef ENGINE_SELECTIONTOPATH_H
#define ENGINE_SELECTIONTOPATH_H

#include "domain/path.h"

#include <QImage>

namespace Ps {

class SelectionToPath
{
public:
    /**
     * 由灰度选区 mask 生成路径（可含多个子路径：外轮廓 + 洞）。
     * 空选区 / 无效尺寸返回空 Path。
     */
    static Path fromMask(const QImage &grayMask, qreal simplifyEpsilon = 1.0);
};

} // namespace Ps

#endif
