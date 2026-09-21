#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <QImage>
#include <QRect>

namespace Ps {

class ImageDocument;

/**
 * 图层合成器（engine）。
 * 只读文档，输出一张预览图；不修改各层 pixels。
 * v1：自底向顶，仅 Normal + opacity；透明底由 Canvas 画棋盘格表现。
 * 日后蒙版/调整层/OpenCV 加速应挂在本模块，而不是 UI。
 */
class Compositor
{
public:
    static QImage composite(const ImageDocument &doc);

    /**
     * @param rect 图像坐标系下的脏区；可只合成局部（整图尺寸仍返回，未覆盖区为透明）。
     */
    static QImage composite(const ImageDocument &doc, const QRect &rect);
};

} // namespace Ps

#endif // COMPOSITOR_H
