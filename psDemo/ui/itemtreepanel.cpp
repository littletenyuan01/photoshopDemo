#include "itemtreepanel.h"

#include "domain/imagedocument.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QSvgRenderer>
#include <QToolButton>

namespace {

/** 透明底棋盘格边长（与画布 CanvasView 的观感一致）。 */
constexpr int kCheckerCell = 6;

/**
 * 把 src 等比缩小到能放进 size 的矩形；**先大比例快速缩放再平滑收尾**。
 * 直接对整张大图做 SmoothTransformation 在缩略图频率下太慢，
 * 两段式（快速降到 2 倍附近，再平滑到目标）视觉等价但快得多。
 */
QImage scaledToFit(const QImage &src, const QSize &size)
{
    if (src.isNull() || size.isEmpty())
        return QImage();

    QSize target = src.size();
    target.scale(size, Qt::KeepAspectRatio);
    if (target.isEmpty())
        return QImage();

    QImage result = src;
    // 目标仍不足源的一半时，先粗缩（快速变换），避免平滑变换在大图上逐像素采样
    if (src.width() > size.width() * 2 || src.height() > size.height() * 2) {
        result = result.scaled(size * 2, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return result.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/** 在透明画布上先铺棋盘格，再把 img 居中叠上去（用于图层缩略图表现透明区）。 */
void paintCheckerboard(QPainter &painter, const QRect &rect)
{
    for (int y = rect.top(); y <= rect.bottom(); y += kCheckerCell) {
        for (int x = rect.left(); x <= rect.right(); x += kCheckerCell) {
            const bool light = ((x / kCheckerCell) + (y / kCheckerCell)) % 2 == 0;
            painter.fillRect(QRect(x, y, kCheckerCell, kCheckerCell).intersected(rect),
                             light ? QColor(0xff, 0xff, 0xff) : QColor(0xcc, 0xcc, 0xcc));
        }
    }
}

} // namespace

ItemTreePanel::ItemTreePanel(QWidget *parent)
    : QWidget(parent)
{
}

ItemTreePanel::~ItemTreePanel() = default;

void ItemTreePanel::bindSkeleton(QFrame *optionsHost, QListWidget *itemList, QFrame *toolbarHost)
{
    // 与 GimpItemTreeView 私有结构中的 options_box / 树 / editor button_box 对齐
    m_optionsHost = optionsHost;
    m_itemList = itemList;
    m_toolbarHost = toolbarHost;
}

QToolButton *ItemTreePanel::addToolbarButton(const QString &objectName,
                                             const QString &text,
                                             const QString &toolTip)
{
    if (!m_toolbarHost)
        return nullptr;

    auto *layout = qobject_cast<QHBoxLayout *>(m_toolbarHost->layout());
    if (!layout)
        return nullptr;

    auto *btn = new QToolButton(m_toolbarHost);
    btn->setObjectName(objectName);
    btn->setText(text);
    btn->setToolTip(toolTip);
    btn->setAutoRaise(true);

    // 插在末尾 spacer 之前，保持「图标左对齐、右侧留白」
    int insertAt = layout->count();
    for (int i = 0; i < layout->count(); ++i) {
        if (layout->itemAt(i)->spacerItem()) {
            insertAt = i;
            break;
        }
    }
    layout->insertWidget(insertAt, btn);
    return btn;
}

void ItemTreePanel::applyToolbarIcon(QToolButton *button,
                                    const QString &resourcePath,
                                    int logicalSize)
{
    if (!button)
        return;
    // SVG 矢量：按目标尺寸直接光栅化，锐利；不再用 48px PNG 缩图。
    // 默认 24px（按钮 30×30，留 3px 边距）—— 比 22px 更易辨认。
    button->setIcon(svgIcon(resourcePath, logicalSize));
    button->setIconSize(QSize(logicalSize, logicalSize));
    button->setText(QString());
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
}

QIcon ItemTreePanel::svgIcon(const QString &resourcePath, int logicalSize)
{
    QIcon icon;
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
        return icon;

    const int scales[] = {1, 2};
    for (int s : scales) {
        const int px = logicalSize * s;

        // 【坑】不要「先设 devicePixelRatio 再 render(painter)」：
        // QSvgRenderer 会按设备尺寸缩放，而 painter 又叠加一次 DPR 变换 →
        // 图标被画成 2 倍大，屏幕上只看到左上角一小块。
        // 正确做法：在与设备像素 1:1 的 QImage 上显式指定目标矩形渲染，最后再给 Pixmap 打 DPR。
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        renderer.render(&painter, QRectF(0, 0, px, px));
        painter.end();

        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(qreal(s));
        icon.addPixmap(pm);
    }
    return icon;
}

// —— 缩略图 ——

QImage ItemTreePanel::makeLayerThumbnail(const QImage &layerPixels)
{
    const QSize box(kThumbSize, kThumbSize);
    if (layerPixels.isNull())
        return QImage();

    const QImage fitted = scaledToFit(layerPixels, box);
    if (fitted.isNull())
        return QImage();

    QImage canvas(box, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    const QRect cell((box.width() - fitted.width()) / 2,
                     (box.height() - fitted.height()) / 2,
                     fitted.width(), fitted.height());
    // 先棋盘格再贴图：图层全透明时也能看出「这里有一层」
    paintCheckerboard(painter, cell);
    painter.drawImage(cell.topLeft(), fitted);
    return canvas;
}

QImage ItemTreePanel::makeChannelThumbnail(const QImage &composite, ThumbChannel channel)
{
    const QSize box(kThumbSize, kThumbSize);
    if (composite.isNull())
        return QImage();

    QImage canvas(box, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (channel == ThumbChannel::Composite) {
        // RGB 行：直接给彩色合成图（透明区仍用棋盘格衬底）
        const QImage fitted = scaledToFit(composite, box);
        if (fitted.isNull())
            return canvas;
        const QRect cell((box.width() - fitted.width()) / 2,
                         (box.height() - fitted.height()) / 2,
                         fitted.width(), fitted.height());
        paintCheckerboard(painter, cell);
        painter.drawImage(cell.topLeft(), fitted);
        return canvas;
    }

    // 分量行：**先把整图降到缩略图的两倍以内，再从缩小后的图取分量**。
    // 【性能 · 实测过】早先是先建一张与文档同尺寸的灰度图逐像素遍历整图：
    // 4000×3000 时每个分量要分配 48 MB 并跑 1200 万次循环，一次刷新 4 个分量
    // 就是 **192 MB 内存抖动 + 4800 万次循环**，单次刷新实测 216 ms（画布本身才 60 ms）。
    // 现在只做「1 次 Qt 降采样（C++ 优化过）+ 80×80 的分量提取 + 1 次小图平滑缩放」。
    const QImage small = scaledToFit(composite, box * 2);
    if (small.isNull())
        return canvas;

    QImage gray(small.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < small.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(gray.scanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QRgb p = src[x];
            int level = 0;
            switch (channel) {
            case ThumbChannel::Red:   level = qRed(p);   break;
            case ThumbChannel::Green: level = qGreen(p); break;
            case ThumbChannel::Blue:  level = qBlue(p);  break;
            case ThumbChannel::Alpha:
                // PS 习惯：白 = 不透明，黑 = 透明，故按 alpha 反转
                level = 255 - qAlpha(p);
                break;
            default: break;
            }
            // 预乘格式下必须连同 alpha 一起写，否则 Qt 会按预乘规则解释颜色
            dst[x] = qRgba(level, level, level, 255);
        }
    }

    const QImage fitted = scaledToFit(gray, box);
    if (!fitted.isNull()) {
        painter.drawImage(QPoint((box.width() - fitted.width()) / 2,
                                 (box.height() - fitted.height()) / 2),
                          fitted);
    }
    return canvas;
}

void ItemTreePanel::setDocument(Ps::ImageDocument *document)
{
    if (m_document == document)
        return;

    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;
    onDocumentChanged();
}

void ItemTreePanel::onDocumentChanged()
{
    refreshFromDocument();
}

void ItemTreePanel::onNewItem()
{
}

void ItemTreePanel::onDeleteItem()
{
}
