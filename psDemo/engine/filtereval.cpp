/**
 * filtereval.cpp — filtereval.h 实现（engine 层）。
 *
 * 按 OpName 分发；亮度/对比度在解预乘空间逐通道调整后再写回预乘像素。
 */
#include "filtereval.h"

#include "domain/filternode.h"
#include "engine/premul.h"

#include <QImage>
#include <QtMath>

namespace Ps {
namespace FilterEval {

namespace {

void applyBrightnessContrast(QImage &image, qreal brightness, qreal contrast)
{
    if (image.isNull() || (qFuzzyIsNull(brightness) && qFuzzyIsNull(contrast)))
        return;

    // 对照常见 BC：contrast 绕 0.5，brightness 加偏移
    const qreal c = 1.0 + contrast;
    const qreal b = brightness;

    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int r, g, bl, a;
            Premul::unpremultiplyRgb(line[x], &r, &g, &bl, &a);
            if (a <= 0)
                continue;

            auto channel = [&](int v) -> int {
                qreal n = v / 255.0;
                n = (n - 0.5) * c + 0.5 + b;
                return qBound(0, int(n * 255.0 + 0.5), 255);
            };
            const QColor col(channel(r), channel(g), channel(bl), a);
            line[x] = Premul::toPremultipliedRgb(col);
        }
    }
}

} // namespace

void applyNode(QImage &image, const FilterNode &node)
{
    switch (node.op()) {
    case OpName::BrightnessContrast:
        applyBrightnessContrast(image, node.brightness(), node.contrast());
        break;
    default:
        break;
    }
}

} // namespace FilterEval
} // namespace Ps
