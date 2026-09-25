#include "colorpickerdialog.h"
#include "ui_colorpickerdialog.h"

#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>

// ---------------------------------------------------------------------------
// 内部绘制控件（嵌入 .ui 中的 planeHost / stripHost / previewHost）
// ---------------------------------------------------------------------------

class ColorPickerDialog::ColorPlaneWidget : public QWidget
{
public:
    explicit ColorPlaneWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(220, 220);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
    }

    void setImage(const QImage &img)
    {
        m_image = img;
        update();
    }

    void setMarker(qreal nx, qreal ny)
    {
        m_nx = qBound(0.0, nx, 1.0);
        m_ny = qBound(0.0, ny, 1.0);
        update();
    }

    std::function<void(qreal, qreal)> onChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (!m_image.isNull())
            p.drawImage(rect(), m_image);
        else
            p.fillRect(rect(), Qt::black);

        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        const QPointF c(m_nx * (width() - 1), m_ny * (height() - 1));
        p.drawEllipse(c, 6, 6);
        p.setPen(QPen(Qt::black, 1.0));
        p.drawEllipse(c, 7, 7);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            updateFromPos(e->position());
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            updateFromPos(e->position());
    }

private:
    void updateFromPos(QPointF pos)
    {
        m_nx = qBound(0.0, pos.x() / qMax(1, width() - 1), 1.0);
        m_ny = qBound(0.0, pos.y() / qMax(1, height() - 1), 1.0);
        update();
        if (onChanged)
            onChanged(m_nx, m_ny);
    }

    QImage m_image;
    qreal m_nx = 0;
    qreal m_ny = 0;
};

class ColorPickerDialog::ColorStripWidget : public QWidget
{
public:
    explicit ColorStripWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(22, 220);
        setCursor(Qt::PointingHandCursor);
    }

    void setImage(const QImage &img)
    {
        m_image = img;
        update();
    }

    void setPos(qreal nz)
    {
        m_nz = qBound(0.0, nz, 1.0);
        update();
    }

    std::function<void(qreal)> onChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect bar(4, 0, width() - 8, height());
        if (!m_image.isNull())
            p.drawImage(bar, m_image);
        else
            p.fillRect(bar, Qt::gray);
        p.setPen(QColor(40, 40, 40));
        p.drawRect(bar.adjusted(0, 0, -1, -1));

        const qreal y = m_nz * (height() - 1);
        QPolygonF left;
        left << QPointF(0, y - 5) << QPointF(4, y) << QPointF(0, y + 5);
        QPolygonF right;
        right << QPointF(width(), y - 5) << QPointF(width() - 4, y)
              << QPointF(width(), y + 5);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPolygon(left);
        p.drawPolygon(right);
        p.setPen(QPen(Qt::black, 0.8));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(left);
        p.drawPolygon(right);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            updateFromPos(e->position());
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            updateFromPos(e->position());
    }

private:
    void updateFromPos(QPointF pos)
    {
        m_nz = qBound(0.0, pos.y() / qMax(1, height() - 1), 1.0);
        update();
        if (onChanged)
            onChanged(m_nz);
    }

    QImage m_image;
    qreal m_nz = 0;
};

class ColorPickerDialog::ColorPreviewWidget : public QWidget
{
public:
    explicit ColorPreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(60, 72);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("点击「当前」可复位"));
    }

    void setNewColor(const QColor &c)
    {
        m_new = c;
        update();
    }
    void setCurrentColor(const QColor &c)
    {
        m_cur = c;
        update();
    }

    std::function<void()> onResetToCurrent;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int mid = height() / 2;
        p.fillRect(0, 0, width(), mid, m_new);
        p.fillRect(0, mid, width(), height() - mid, m_cur);
        p.setPen(QColor(30, 30, 30));
        p.drawRect(0, 0, width() - 1, height() - 1);
        p.drawLine(0, mid, width(), mid);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && e->position().y() >= height() / 2.0
            && onResetToCurrent) {
            onResetToCurrent();
        }
    }

private:
    QColor m_new {Qt::white};
    QColor m_cur {Qt::white};
};

static void embedIntoHost(QWidget *host, QWidget *child)
{
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(child);
}

// ---------------------------------------------------------------------------
// 颜色空间辅助
// ---------------------------------------------------------------------------

static qreal srgbToLinear(qreal c)
{
    c = qBound(0.0, c, 1.0);
    return (c <= 0.04045) ? (c / 12.92) : qPow((c + 0.055) / 1.055, 2.4);
}

static qreal linearToSrgb(qreal c)
{
    c = qBound(0.0, c, 1.0);
    return (c <= 0.0031308) ? (12.92 * c) : (1.055 * qPow(c, 1.0 / 2.4) - 0.055);
}

void ColorPickerDialog::rgbToLab(const QColor &col, qreal *L, qreal *a, qreal *b)
{
    const qreal r = srgbToLinear(col.redF());
    const qreal g = srgbToLinear(col.greenF());
    const qreal bl = srgbToLinear(col.blueF());

    qreal x = r * 0.4124564 + g * 0.3575761 + bl * 0.1804375;
    qreal y = r * 0.2126729 + g * 0.7151522 + bl * 0.0721750;
    qreal z = r * 0.0193339 + g * 0.1191920 + bl * 0.9503041;

    x /= 0.95047;
    y /= 1.00000;
    z /= 1.08883;

    auto f = [](qreal t) {
        return (t > 0.008856) ? qPow(t, 1.0 / 3.0)
                              : (7.787 * t + 16.0 / 116.0);
    };
    const qreal fx = f(x);
    const qreal fy = f(y);
    const qreal fz = f(z);

    *L = 116.0 * fy - 16.0;
    *a = 500.0 * (fx - fy);
    *b = 200.0 * (fy - fz);
}

QColor ColorPickerDialog::labToRgb(qreal L, qreal a, qreal b)
{
    const qreal fy = (L + 16.0) / 116.0;
    const qreal fx = fy + a / 500.0;
    const qreal fz = fy - b / 200.0;

    auto finv = [](qreal t) {
        const qreal t3 = t * t * t;
        return (t3 > 0.008856) ? t3 : ((t - 16.0 / 116.0) / 7.787);
    };

    qreal x = 0.95047 * finv(fx);
    qreal y = 1.00000 * finv(fy);
    qreal z = 1.08883 * finv(fz);

    qreal rl = x * 3.2404542 + y * -1.5371385 + z * -0.4985314;
    qreal gl = x * -0.9692660 + y * 1.8760108 + z * 0.0415560;
    qreal bl = x * 0.0556434 + y * -0.2040259 + z * 1.0572252;

    return QColor::fromRgbF(linearToSrgb(rl), linearToSrgb(gl), linearToSrgb(bl));
}

void ColorPickerDialog::rgbToCmyk(const QColor &c, int *C, int *M, int *Y, int *K)
{
    const qreal r = c.redF();
    const qreal g = c.greenF();
    const qreal b = c.blueF();
    const qreal k = 1.0 - qMax(r, qMax(g, b));
    if (k >= 0.9999) {
        *C = *M = *Y = 0;
        *K = 100;
        return;
    }
    *C = qRound((1.0 - r - k) / (1.0 - k) * 100.0);
    *M = qRound((1.0 - g - k) / (1.0 - k) * 100.0);
    *Y = qRound((1.0 - b - k) / (1.0 - k) * 100.0);
    *K = qRound(k * 100.0);
}

QColor ColorPickerDialog::cmykToRgb(int C, int M, int Y, int K)
{
    const qreal c = qBound(0, C, 100) / 100.0;
    const qreal m = qBound(0, M, 100) / 100.0;
    const qreal y = qBound(0, Y, 100) / 100.0;
    const qreal k = qBound(0, K, 100) / 100.0;
    return QColor::fromRgbF((1.0 - c) * (1.0 - k),
                            (1.0 - m) * (1.0 - k),
                            (1.0 - y) * (1.0 - k));
}

QColor ColorPickerDialog::nearestWebSafe(const QColor &c)
{
    auto snap = [](int v) {
        static const int steps[] = {0, 51, 102, 153, 204, 255};
        int best = steps[0];
        int bestD = qAbs(v - best);
        for (int s : steps) {
            const int d = qAbs(v - s);
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        return best;
    };
    return QColor(snap(c.red()), snap(c.green()), snap(c.blue()));
}

// ---------------------------------------------------------------------------
// ColorPickerDialog
// ---------------------------------------------------------------------------

ColorPickerDialog::ColorPickerDialog(const QColor &initial, Mode mode, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ColorPickerDialog)
    , m_color(initial.isValid() ? initial : QColor(Qt::white))
    , m_original(m_color)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui->setupUi(this);
    wireUi(mode);
    setColorInternal(m_color, true);
}

ColorPickerDialog::~ColorPickerDialog()
{
    delete ui;
}

QColor ColorPickerDialog::getColor(const QColor &initial, QWidget *parent, Mode mode)
{
    ColorPickerDialog dlg(initial, mode, parent);
    if (dlg.exec() == QDialog::Accepted)
        return dlg.selectedColor();
    return {};
}

void ColorPickerDialog::wireUi(Mode mode)
{
    setWindowTitle(mode == Mode::Foreground
                       ? tr("拾色器（前景色）")
                       : tr("拾色器（背景色）"));

    m_plane = new ColorPlaneWidget(ui->planeHost);
    m_plane->onChanged = [this](qreal x, qreal y) { onFieldChanged(x, y); };
    embedIntoHost(ui->planeHost, m_plane);

    m_strip = new ColorStripWidget(ui->stripHost);
    m_strip->onChanged = [this](qreal z) { onSliderChanged(z); };
    embedIntoHost(ui->stripHost, m_strip);

    m_preview = new ColorPreviewWidget(ui->previewHost);
    m_preview->setCurrentColor(m_original);
    m_preview->setNewColor(m_color);
    m_preview->onResetToCurrent = [this]() { onReset(); };
    embedIntoHost(ui->previewHost, m_preview);

    connect(ui->webOnly, &QCheckBox::toggled, this, [this](bool) {
        applyWebSafeIfNeeded();
        setColorInternal(m_color, true);
    });

    connect(ui->okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->resetBtn, &QPushButton::clicked, this, &ColorPickerDialog::onReset);
    connect(ui->addBtn, &QPushButton::clicked, this, &ColorPickerDialog::onAddToSwatches);
    connect(ui->libBtn, &QPushButton::clicked, this, &ColorPickerDialog::onColorLibraries);

    for (QRadioButton *rb : {ui->rbH, ui->rbS, ui->rbV, ui->rbR, ui->rbG, ui->rbB,
                             ui->rbL, ui->rbA, ui->rbLabB}) {
        connect(rb, &QRadioButton::toggled, this, &ColorPickerDialog::onChannelToggled);
    }

    connect(ui->edH, &QLineEdit::editingFinished, this, &ColorPickerDialog::onHsVEdited);
    connect(ui->edS, &QLineEdit::editingFinished, this, &ColorPickerDialog::onHsVEdited);
    connect(ui->edV, &QLineEdit::editingFinished, this, &ColorPickerDialog::onHsVEdited);
    connect(ui->edR, &QLineEdit::editingFinished, this, &ColorPickerDialog::onRgbEdited);
    connect(ui->edG, &QLineEdit::editingFinished, this, &ColorPickerDialog::onRgbEdited);
    connect(ui->edB, &QLineEdit::editingFinished, this, &ColorPickerDialog::onRgbEdited);
    connect(ui->edL, &QLineEdit::editingFinished, this, &ColorPickerDialog::onLabEdited);
    connect(ui->edA, &QLineEdit::editingFinished, this, &ColorPickerDialog::onLabEdited);
    connect(ui->edLabB, &QLineEdit::editingFinished, this, &ColorPickerDialog::onLabEdited);
    connect(ui->edC, &QLineEdit::editingFinished, this, &ColorPickerDialog::onCmykEdited);
    connect(ui->edM, &QLineEdit::editingFinished, this, &ColorPickerDialog::onCmykEdited);
    connect(ui->edY, &QLineEdit::editingFinished, this, &ColorPickerDialog::onCmykEdited);
    connect(ui->edK, &QLineEdit::editingFinished, this, &ColorPickerDialog::onCmykEdited);
    connect(ui->edHex, &QLineEdit::editingFinished, this, &ColorPickerDialog::onHexEdited);
}

void ColorPickerDialog::channelMapToXyz(qreal *x, qreal *y, qreal *z) const
{
    int h = 0, s = 0, v = 0;
    m_color.getHsv(&h, &s, &v);
    if (h < 0)
        h = 0;
    const qreal hf = h / 360.0;
    const qreal sf = s / 255.0;
    const qreal vf = v / 255.0;
    const qreal rf = m_color.redF();
    const qreal gf = m_color.greenF();
    const qreal bf = m_color.blueF();
    qreal L = 0, a = 0, bb = 0;
    rgbToLab(m_color, &L, &a, &bb);

    switch (m_channel) {
    case Channel::H:
        *x = sf;
        *y = 1.0 - vf;
        *z = 1.0 - hf;
        break;
    case Channel::S:
        *x = hf;
        *y = 1.0 - vf;
        *z = 1.0 - sf;
        break;
    case Channel::V:
        *x = hf;
        *y = 1.0 - sf;
        *z = 1.0 - vf;
        break;
    case Channel::R:
        *x = bf;
        *y = 1.0 - gf;
        *z = 1.0 - rf;
        break;
    case Channel::G:
        *x = bf;
        *y = 1.0 - rf;
        *z = 1.0 - gf;
        break;
    case Channel::B:
        *x = rf;
        *y = 1.0 - gf;
        *z = 1.0 - bf;
        break;
    case Channel::L:
        *x = (a + 128.0) / 255.0;
        *y = 1.0 - (bb + 128.0) / 255.0;
        *z = 1.0 - L / 100.0;
        break;
    case Channel::A:
        *x = L / 100.0;
        *y = 1.0 - (bb + 128.0) / 255.0;
        *z = 1.0 - (a + 128.0) / 255.0;
        break;
    case Channel::LabB:
        *x = L / 100.0;
        *y = 1.0 - (a + 128.0) / 255.0;
        *z = 1.0 - (bb + 128.0) / 255.0;
        break;
    }
}

QColor ColorPickerDialog::colorFromChannelMap(qreal x, qreal y, qreal z) const
{
    x = qBound(0.0, x, 1.0);
    y = qBound(0.0, y, 1.0);
    z = qBound(0.0, z, 1.0);

    int h = 0, s = 0, v = 0;
    m_color.getHsv(&h, &s, &v);
    if (h < 0)
        h = 0;

    switch (m_channel) {
    case Channel::H:
        return QColor::fromHsv(qRound((1.0 - z) * 359),
                               qRound(x * 255),
                               qRound((1.0 - y) * 255));
    case Channel::S:
        return QColor::fromHsv(qRound(x * 359),
                               qRound((1.0 - z) * 255),
                               qRound((1.0 - y) * 255));
    case Channel::V:
        return QColor::fromHsv(qRound(x * 359),
                               qRound((1.0 - y) * 255),
                               qRound((1.0 - z) * 255));
    case Channel::R:
        return QColor::fromRgbF(1.0 - z, 1.0 - y, x);
    case Channel::G:
        return QColor::fromRgbF(1.0 - y, 1.0 - z, x);
    case Channel::B:
        return QColor::fromRgbF(x, 1.0 - y, 1.0 - z);
    case Channel::L:
        return labToRgb((1.0 - z) * 100.0,
                        x * 255.0 - 128.0,
                        (1.0 - y) * 255.0 - 128.0);
    case Channel::A:
        return labToRgb(x * 100.0,
                        (1.0 - z) * 255.0 - 128.0,
                        (1.0 - y) * 255.0 - 128.0);
    case Channel::LabB:
        return labToRgb(x * 100.0,
                        (1.0 - y) * 255.0 - 128.0,
                        (1.0 - z) * 255.0 - 128.0);
    }
    return m_color;
}

void ColorPickerDialog::rebuildGradients()
{
    const int N = 256;
    QImage plane(N, N, QImage::Format_RGB32);
    QImage strip(1, N, QImage::Format_RGB32);

    qreal cx = 0, cy = 0, cz = 0;
    channelMapToXyz(&cx, &cy, &cz);

    for (int yi = 0; yi < N; ++yi) {
        const qreal ny = yi / qreal(N - 1);
        for (int xi = 0; xi < N; ++xi) {
            const qreal nx = xi / qreal(N - 1);
            plane.setPixelColor(xi, yi, colorFromChannelMap(nx, ny, cz));
        }
        strip.setPixelColor(0, yi, colorFromChannelMap(cx, cy, yi / qreal(N - 1)));
    }

    if (m_channel == Channel::H) {
        for (int yi = 0; yi < N; ++yi) {
            const int hue = qRound((1.0 - yi / qreal(N - 1)) * 359);
            strip.setPixelColor(0, yi, QColor::fromHsv(hue, 255, 255));
        }
    }

    m_plane->setImage(plane);
    m_plane->setMarker(cx, cy);
    m_strip->setImage(strip);
    m_strip->setPos(cz);
}

void ColorPickerDialog::setColorInternal(const QColor &c, bool updateFields)
{
    m_color = c;
    if (!m_color.isValid())
        m_color = Qt::white;
    m_color = QColor(m_color.rgb());

    if (ui->webOnly->isChecked())
        m_color = nearestWebSafe(m_color);

    m_preview->setNewColor(m_color);
    rebuildGradients();

    if (updateFields)
        syncFieldsFromColor();
}

void ColorPickerDialog::syncFieldsFromColor()
{
    m_updating = true;

    int h = 0, s = 0, v = 0;
    m_color.getHsv(&h, &s, &v);
    if (h < 0)
        h = 0;

    ui->edH->setText(QString::number(h));
    ui->edS->setText(QString::number(qRound(s / 255.0 * 100)));
    ui->edV->setText(QString::number(qRound(v / 255.0 * 100)));

    ui->edR->setText(QString::number(m_color.red()));
    ui->edG->setText(QString::number(m_color.green()));
    ui->edB->setText(QString::number(m_color.blue()));

    qreal L = 0, a = 0, b = 0;
    rgbToLab(m_color, &L, &a, &b);
    ui->edL->setText(QString::number(qRound(L)));
    ui->edA->setText(QString::number(qRound(a)));
    ui->edLabB->setText(QString::number(qRound(b)));

    int C = 0, M = 0, Y = 0, K = 0;
    rgbToCmyk(m_color, &C, &M, &Y, &K);
    ui->edC->setText(QString::number(C));
    ui->edM->setText(QString::number(M));
    ui->edY->setText(QString::number(Y));
    ui->edK->setText(QString::number(K));

    ui->edHex->setText(QStringLiteral("%1").arg(m_color.rgb() & 0xFFFFFF, 6, 16, QLatin1Char('0')));

    m_updating = false;
}

void ColorPickerDialog::applyWebSafeIfNeeded()
{
    if (ui->webOnly->isChecked())
        m_color = nearestWebSafe(m_color);
}

void ColorPickerDialog::onFieldChanged(qreal nx, qreal ny)
{
    qreal cx = 0, cy = 0, cz = 0;
    channelMapToXyz(&cx, &cy, &cz);
    Q_UNUSED(cx);
    Q_UNUSED(cy);
    setColorInternal(colorFromChannelMap(nx, ny, cz), true);
}

void ColorPickerDialog::onSliderChanged(qreal nz)
{
    qreal cx = 0, cy = 0, cz = 0;
    channelMapToXyz(&cx, &cy, &cz);
    Q_UNUSED(cz);
    setColorInternal(colorFromChannelMap(cx, cy, nz), true);
}

void ColorPickerDialog::onChannelToggled()
{
    if (m_updating)
        return;
    if (ui->rbH->isChecked())
        m_channel = Channel::H;
    else if (ui->rbS->isChecked())
        m_channel = Channel::S;
    else if (ui->rbV->isChecked())
        m_channel = Channel::V;
    else if (ui->rbR->isChecked())
        m_channel = Channel::R;
    else if (ui->rbG->isChecked())
        m_channel = Channel::G;
    else if (ui->rbB->isChecked())
        m_channel = Channel::B;
    else if (ui->rbL->isChecked())
        m_channel = Channel::L;
    else if (ui->rbA->isChecked())
        m_channel = Channel::A;
    else if (ui->rbLabB->isChecked())
        m_channel = Channel::LabB;
    rebuildGradients();
}

void ColorPickerDialog::onHsVEdited()
{
    if (m_updating)
        return;
    const int h = qBound(0, ui->edH->text().toInt(), 359);
    const int s = qBound(0, qRound(ui->edS->text().toInt() / 100.0 * 255), 255);
    const int v = qBound(0, qRound(ui->edV->text().toInt() / 100.0 * 255), 255);
    setColorInternal(QColor::fromHsv(h, s, v), true);
}

void ColorPickerDialog::onRgbEdited()
{
    if (m_updating)
        return;
    setColorInternal(QColor(qBound(0, ui->edR->text().toInt(), 255),
                            qBound(0, ui->edG->text().toInt(), 255),
                            qBound(0, ui->edB->text().toInt(), 255)),
                     true);
}

void ColorPickerDialog::onLabEdited()
{
    if (m_updating)
        return;
    const qreal L = qBound(0.0, ui->edL->text().toDouble(), 100.0);
    const qreal a = qBound(-128.0, ui->edA->text().toDouble(), 127.0);
    const qreal b = qBound(-128.0, ui->edLabB->text().toDouble(), 127.0);
    setColorInternal(labToRgb(L, a, b), true);
}

void ColorPickerDialog::onCmykEdited()
{
    if (m_updating)
        return;
    setColorInternal(cmykToRgb(ui->edC->text().toInt(),
                               ui->edM->text().toInt(),
                               ui->edY->text().toInt(),
                               ui->edK->text().toInt()),
                     true);
}

void ColorPickerDialog::onHexEdited()
{
    if (m_updating)
        return;
    QString t = ui->edHex->text().trimmed();
    if (t.startsWith(QLatin1Char('#')))
        t = t.mid(1);
    if (t.size() == 3)
        t = QString(t[0]) + t[0] + t[1] + t[1] + t[2] + t[2];
    bool ok = false;
    const uint rgb = t.toUInt(&ok, 16);
    if (ok && t.size() == 6)
        setColorInternal(QColor::fromRgb(int(rgb)), true);
    else
        syncFieldsFromColor();
}

void ColorPickerDialog::onReset()
{
    setColorInternal(m_original, true);
}

void ColorPickerDialog::onAddToSwatches()
{
    QMessageBox::information(this, tr("添加到色板"),
                             tr("已记录颜色 %1（色板面板尚未接入）。")
                                 .arg(m_color.name()));
}

void ColorPickerDialog::onColorLibraries()
{
    QMessageBox::information(this, tr("颜色库"),
                             tr("颜色库功能稍后接入（Pantone 等）。"));
}
