#include "rulerwidget.h"

#include <QPainter>
#include <QtMath>

#include <cmath>
#include <limits>

namespace {
constexpr qreal kNaN = std::numeric_limits<qreal>::quiet_NaN();
}

RulerWidget::RulerWidget(QWidget *parent)
    : QWidget(parent)
    , m_cursor(kNaN)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void RulerWidget::setOrientation(Qt::Orientation orientation)
{
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;
    update();
}

void RulerWidget::setRange(qreal lower, qreal upper)
{
    if (qFuzzyCompare(m_lower, lower) && qFuzzyCompare(m_upper, upper))
        return;
    m_lower = lower;
    m_upper = qFuzzyCompare(lower, upper) ? lower + 1.0 : upper;
    update();
}

void RulerWidget::setCursorValue(qreal value)
{
    if (std::isnan(m_cursor) && std::isnan(value))
        return;
    if (!std::isnan(m_cursor) && !std::isnan(value) && qFuzzyCompare(m_cursor, value))
        return;
    m_cursor = value;
    update();
}

qreal RulerWidget::niceStep(qreal rawStep)
{
    if (rawStep <= 0.0)
        return 1.0;
    const qreal exp = qFloor(std::log10(rawStep));
    const qreal base = qPow(10.0, exp);
    const qreal n = rawStep / base;
    if (n <= 1.0)
        return base;
    if (n <= 2.0)
        return 2.0 * base;
    if (n <= 5.0)
        return 5.0 * base;
    return 10.0 * base;
}

void RulerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x4a, 0x4a, 0x4a));

    p.setPen(QColor(0x2a, 0x2a, 0x2a));
    if (m_orientation == Qt::Horizontal)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());

    const int length = (m_orientation == Qt::Horizontal) ? width() : height();
    if (length <= 1)
        return;

    const qreal span = m_upper - m_lower;
    if (span <= 0.0)
        return;

    const qreal major = niceStep(50.0 * span / length);
    const qreal minor = major / 5.0;

    p.setFont(QFont(QStringLiteral("Segoe UI"), 8));
    const QColor tickColor(0xcc, 0xcc, 0xcc);
    const QColor textColor(0xdd, 0xdd, 0xdd);

    const int firstIdx = qFloor(m_lower / minor);
    const int lastIdx = qCeil(m_upper / minor);
    for (int i = firstIdx; i <= lastIdx; ++i) {
        const qreal v = i * minor;
        const qreal t = (v - m_lower) / span;
        const int pos = qRound(t * length);
        if (pos < -2 || pos > length + 2)
            continue;

        const bool majorTick = (i % 5 == 0);
        p.setPen(tickColor);
        if (m_orientation == Qt::Horizontal) {
            const int tickH = majorTick ? height() - 4 : qMax(4, height() / 3);
            p.drawLine(pos, height() - 1, pos, height() - 1 - tickH);
            if (majorTick) {
                p.setPen(textColor);
                p.drawText(pos + 2, 11, QString::number(qRound(v)));
            }
        } else {
            const int tickW = majorTick ? width() - 4 : qMax(4, width() / 3);
            p.drawLine(width() - 1, pos, width() - 1 - tickW, pos);
            if (majorTick) {
                p.setPen(textColor);
                p.drawText(2, pos - 2, QString::number(qRound(v)));
            }
        }
    }

    if (!std::isnan(m_cursor)) {
        const qreal t = (m_cursor - m_lower) / span;
        const int pos = qRound(t * length);
        if (pos >= 0 && pos < length) {
            p.setPen(QColor(0x2d, 0x9c, 0xdb));
            if (m_orientation == Qt::Horizontal)
                p.drawLine(pos, 0, pos, height());
            else
                p.drawLine(0, pos, width(), pos);
        }
    }
}
