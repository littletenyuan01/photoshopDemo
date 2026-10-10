/**
 * blendop.h — 图层混合图算子（engine/graph 层）。
 *
 * 2 输入（backdrop=下方合成结果、layer=本层），按混合模式/不透明度合成；
 * 可选蒙版源。逐像素走 engine/compositepixel.h，与 Compositor 合成路径一致。
 * 对照 GEGL gimp:layer-mode / composite_union。
 */
#ifndef ENGINE_GRAPH_BLENDOP_H
#define ENGINE_GRAPH_BLENDOP_H

#include "graphop.h"
#include "graphsource.h"

#include "domain/blendmode.h"
#include "engine/compositepixel.h"

#include <QImage>
#include <QRect>
#include <memory>

namespace Ps {

/**
 * 图层混合算子（对照 gimp:layer-mode）。
 * 输入 0 = backdrop（下方已合成），输入 1 = layer（本层）；两者同尺寸（ROI 对齐）。
 * @p mask 可选：灰度蒙版源（文档坐标，Format_Grayscale8）；nullptr = 无蒙版。
 */
class BlendOp : public GraphOp
{
public:
    BlendOp(BlendMode mode, qreal opacity, std::shared_ptr<const GraphSource> mask = nullptr)
        : m_mode(mode), m_opacity(opacity), m_mask(std::move(mask)) {}

    int inputCount() const override { return 2; }

    QVector<QRect> requiredInputs(const QRect &out) const override { return { out, out }; }

    QImage process(const QVector<QImage> &ins, const QRect &out) const override
    {
        const QImage backdrop = ins.value(0);
        const QImage layer = ins.value(1);
        if (backdrop.isNull())
            return layer;
        if (layer.isNull())
            return backdrop;

        QImage result = backdrop; // 隐式共享，写时拷贝
        if (result.format() != QImage::Format_ARGB32_Premultiplied)
            result = result.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        const bool dissolving = (m_mode == BlendMode::Dissolve);
        const QImage maskImg = m_mask ? m_mask->read(out) : QImage();

        const int w = out.width();
        const int h = out.height();
        for (int y = 0; y < h; ++y) {
            QRgb *dline = reinterpret_cast<QRgb *>(result.scanLine(y));
            const QRgb *bline = reinterpret_cast<const QRgb *>(backdrop.constScanLine(y));
            const QRgb *lline = reinterpret_cast<const QRgb *>(layer.constScanLine(y));
            const uchar *mline = nullptr;
            if (!maskImg.isNull() && maskImg.format() == QImage::Format_Grayscale8)
                mline = maskImg.constScanLine(y);
            for (int x = 0; x < w; ++x) {
                const int maskV = mline ? int(mline[x]) : -1;
                dline[x] = compositePixelOnto(bline[x], lline[x], m_opacity, m_mode,
                                              dissolving, out.left() + x, out.top() + y,
                                              maskV);
            }
        }
        return result;
    }

private:
    BlendMode m_mode;
    qreal m_opacity;
    std::shared_ptr<const GraphSource> m_mask; ///< 可选灰度蒙版源（文档坐标）
};

} // namespace Ps

#endif // ENGINE_GRAPH_BLENDOP_H
