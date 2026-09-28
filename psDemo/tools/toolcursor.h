#ifndef TOOLCURSOR_H
#define TOOLCURSOR_H

#include <QCursor>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace Ps {
namespace ToolCursor {

/** 把工具箱图标做成画布光标：近黑底透明，热点按比例定位。 */
inline QCursor fromToolIcon(const QString &resourcePath,
                            qreal hotX = 0.5, qreal hotY = 0.5,
                            int logicalSize = 28)
{
    QPixmap src(resourcePath);
    if (src.isNull())
        return QCursor(Qt::CrossCursor);

    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int r = qRed(line[x]);
            const int g = qGreen(line[x]);
            const int b = qBlue(line[x]);
            // 工具图标多为深色底 + 浅色线稿；把近黑像素抠成透明
            if (r < 48 && g < 48 && b < 48)
                line[x] = qRgba(0, 0, 0, 0);
        }
    }

    const QPixmap pm = QPixmap::fromImage(img).scaled(
        logicalSize, logicalSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int hx = qBound(0, int(pm.width() * hotX), pm.width() - 1);
    const int hy = qBound(0, int(pm.height() * hotY), pm.height() - 1);
    return QCursor(pm, hx, hy);
}

/** PS 风格渐变光标：中心十字 + 右下角小渐变条。 */
inline QCursor gradientStyle()
{
    constexpr int s = 32;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);

    auto drawCross = [&](const QColor &c, int t) {
        p.setPen(QPen(c, t));
        p.drawLine(16, 2, 16, 12);
        p.drawLine(16, 20, 16, 30);
        p.drawLine(2, 16, 12, 16);
        p.drawLine(20, 16, 30, 16);
    };
    drawCross(Qt::black, 3);
    drawCross(Qt::white, 1);

    QLinearGradient g(18, 18, 30, 30);
    g.setColorAt(0.0, Qt::black);
    g.setColorAt(1.0, Qt::white);
    p.fillRect(QRect(20, 20, 10, 10), g);
    p.setPen(QColor(0, 0, 0));
    p.drawRect(20, 20, 9, 9);

    p.end();
    return QCursor(pm, 16, 16);
}

} // namespace ToolCursor
} // namespace Ps

#endif // TOOLCURSOR_H
