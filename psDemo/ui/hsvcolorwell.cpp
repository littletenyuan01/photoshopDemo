#include "hsvcolorwell.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QtMath>

namespace {

constexpr int kSwatch = 28;
constexpr int kSwatchGap = 10;   // 前景/背景错位
constexpr int kHueW = 16;
constexpr int kPad = 6;
constexpr int kBetween = 8;

} // namespace

HsvColorWell::HsvColorWell(QWidget *parent)
    : QWidget(parent)
{
    // 尺寸下限统一由 minimumSizeHint() 给，不再另外 setMinimumHeight（两处会打架）
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    adoptColor(m_color);
}

/** 把 color 收进来并同步 HSV 三分量（h<0 表示无色相，此时保留原色相）。 */
void HsvColorWell::adoptColor(const QColor &color)
{
    m_color = color.toRgb();
    int h = 0, s = 0, v = 0;
    m_color.getHsv(&h, &s, &v);
    if (h >= 0)
        m_hue = h;
    m_sat = s;
    m_val = v;
}

void HsvColorWell::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    adoptColor(color);
    rebuildCaches();
    update();
    emit colorChanged(m_color);
}

void HsvColorWell::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || color == m_bg)
        return;
    m_bg = color.toRgb();
    update();
}

QSize HsvColorWell::sizeHint() const
{
    return QSize(260, 140);
}

QSize HsvColorWell::minimumSizeHint() const
{
    return QSize(180, 110);
}

QRect HsvColorWell::swatchFgRect() const
{
    return QRect(kPad, kPad, kSwatch, kSwatch);
}

QRect HsvColorWell::swatchBgRect() const
{
    return QRect(kPad + kSwatchGap, kPad + kSwatchGap, kSwatch, kSwatch);
}

QRect HsvColorWell::fieldRect() const
{
    const int left = kPad + kSwatch + kSwatchGap + kBetween;
    const int right = width() - kPad - kHueW - kBetween;
    const int top = kPad;
    const int bottom = height() - kPad;
    return QRect(QPoint(left, top), QPoint(right, bottom)).normalized();
}

QRect HsvColorWell::hueRect() const
{
    return QRect(width() - kPad - kHueW, kPad, kHueW, height() - 2 * kPad);
}

QPointF HsvColorWell::fieldCursor() const
{
    const QRect r = fieldRect();
    if (!r.isValid())
        return r.center();
    const qreal x = r.left() + (m_sat / 255.0) * (r.width() - 1);
    const qreal y = r.top() + (1.0 - m_val / 255.0) * (r.height() - 1);
    return QPointF(x, y);
}

void HsvColorWell::rebuildCaches()
{
    const QRect fr = fieldRect();
    const QRect hr = hueRect();
    if (fr.width() < 2 || fr.height() < 2 || hr.height() < 2)
        return;

    m_fieldCache = QImage(fr.size(), QImage::Format_RGB32);
    for (int y = 0; y < fr.height(); ++y) {
        const int v = 255 - qRound(y * 255.0 / qMax(1, fr.height() - 1));
        QRgb *line = reinterpret_cast<QRgb *>(m_fieldCache.scanLine(y));
        for (int x = 0; x < fr.width(); ++x) {
            const int s = qRound(x * 255.0 / qMax(1, fr.width() - 1));
            line[x] = QColor::fromHsv(m_hue, s, v).rgb();
        }
    }

    m_hueCache = QImage(hr.size(), QImage::Format_RGB32);
    for (int y = 0; y < hr.height(); ++y) {
        const int h = qRound((1.0 - y * 1.0 / qMax(1, hr.height() - 1)) * 359);
        const QRgb c = QColor::fromHsv(h, 255, 255).rgb();
        QRgb *line = reinterpret_cast<QRgb *>(m_hueCache.scanLine(y));
        for (int x = 0; x < hr.width(); ++x)
            line[x] = c;
    }
}

void HsvColorWell::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildCaches();
}

void HsvColorWell::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景色块在下、前景色块在上（PS：错位叠放）
    const QRect bg = swatchBgRect();
    const QRect fg = swatchFgRect();
    p.fillRect(bg.adjusted(1, 1, -1, -1), m_bg);
    p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
    p.drawRect(bg.adjusted(0, 0, -1, -1));
    p.fillRect(fg.adjusted(1, 1, -1, -1), m_color);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(fg.adjusted(1, 1, -2, -2));
    p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
    p.drawRect(fg.adjusted(0, 0, -1, -1));

    // 左下角小双箭头暗示可点交换（简化 PS 的弯箭头）
    p.setPen(QPen(QColor(0xc0, 0xc0, 0xc0), 1.2));
    const QPoint a(fg.left() + 2, fg.bottom() + 4);
    p.drawLine(a, a + QPoint(8, 0));
    p.drawLine(a + QPoint(8, 0), a + QPoint(5, -3));
    p.drawLine(a + QPoint(8, 0), a + QPoint(5, 3));

    const QRect fr = fieldRect();
    if (!m_fieldCache.isNull())
        p.drawImage(fr, m_fieldCache);
    p.setPen(QColor(0x20, 0x20, 0x20));
    p.setBrush(Qt::NoBrush);
    p.drawRect(fr.adjusted(0, 0, -1, -1));

    // 准星
    const QPointF c = fieldCursor();
    p.setPen(QPen(Qt::white, 1.5));
    p.drawEllipse(c, 5, 5);
    p.setPen(QPen(Qt::black, 1.0));
    p.drawEllipse(c, 6, 6);

    const QRect hr = hueRect();
    if (!m_hueCache.isNull())
        p.drawImage(hr, m_hueCache);
    p.setPen(QColor(0x20, 0x20, 0x20));
    p.drawRect(hr.adjusted(0, 0, -1, -1));

    // 色相指示三角
    const qreal hy = hr.top() + (1.0 - m_hue / 359.0) * (hr.height() - 1);
    QPolygonF tri;
    tri << QPointF(hr.right() + 1, hy)
        << QPointF(hr.right() + 7, hy - 4)
        << QPointF(hr.right() + 7, hy + 4);
    p.setBrush(Qt::white);
    p.setPen(QPen(Qt::black, 1));
    p.drawPolygon(tri);
}

void HsvColorWell::applyFromFieldPos(QPoint pos)
{
    const QRect r = fieldRect();
    if (!r.isValid())
        return;
    const qreal nx = qBound(0.0, (pos.x() - r.left()) * 1.0 / qMax(1, r.width() - 1), 1.0);
    const qreal ny = qBound(0.0, (pos.y() - r.top()) * 1.0 / qMax(1, r.height() - 1), 1.0);
    m_sat = qRound(nx * 255);
    m_val = qRound((1.0 - ny) * 255);
    m_color = QColor::fromHsv(m_hue, m_sat, m_val);
    update();
    emit colorChanged(m_color);
}

void HsvColorWell::applyFromHuePos(QPoint pos)
{
    const QRect r = hueRect();
    if (!r.isValid())
        return;
    const qreal ny = qBound(0.0, (pos.y() - r.top()) * 1.0 / qMax(1, r.height() - 1), 1.0);
    m_hue = qRound((1.0 - ny) * 359);
    m_color = QColor::fromHsv(m_hue, m_sat, m_val);
    rebuildCaches();
    update();
    emit colorChanged(m_color);
}

void HsvColorWell::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = event->position().toPoint();

    // 点在前景/背景叠放区左下 → 交换
    if (QRect(swatchFgRect().left(), swatchFgRect().bottom(), 14, 12).contains(pos)
        || (swatchBgRect().contains(pos) && !swatchFgRect().contains(pos))) {
        const QColor tmp = m_color;
        adoptColor(m_bg);
        m_bg = tmp;
        rebuildCaches();
        update();
        emit swatchesSwapped();
        emit colorChanged(m_color);
        return;
    }

    if (fieldRect().contains(pos)) {
        m_drag = DragTarget::Field;
        applyFromFieldPos(pos);
    } else if (hueRect().contains(pos)) {
        m_drag = DragTarget::Hue;
        applyFromHuePos(pos);
    }
}

void HsvColorWell::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPoint pos = event->position().toPoint();
    if (m_drag == DragTarget::Field)
        applyFromFieldPos(pos);
    else if (m_drag == DragTarget::Hue)
        applyFromHuePos(pos);
}

void HsvColorWell::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_drag = DragTarget::None;
    QWidget::mouseReleaseEvent(event);
}
