/**
 * infopanel.cpp — InfoPanel 实现（ui 层）。
 */
#include "infopanel.h"
#include "ui_infopanel.h"

#include "app/appsession.h"
#include "domain/imagedocument.h"
#include "domain/layer.h"
#include "domain/layerstack.h"
#include "domain/selection.h"
#include "domain/tilebuffer.h"

#include <QTabWidget>
#include <QtMath>

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::InfoPanel)
{
    ui->setupUi(this);
    ui->panelTabs->setCornerWidget(ui->cornerButtons, Qt::TopRightCorner);
    clearColorReadout();
    ui->labelXVal->clear();
    ui->labelYVal->clear();
    ui->labelWVal->clear();
    ui->labelHVal->clear();
    ui->labelDocSize->setText(QStringLiteral("文档:—"));
    ui->labelToolHint->clear();
}

InfoPanel::~InfoPanel()
{
    delete ui;
}

void InfoPanel::setSession(Ps::AppSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    if (m_session) {
        connect(m_session, &Ps::AppSession::documentChanged,
                this, &InfoPanel::onSessionDocumentChanged);
    }
    onSessionDocumentChanged(m_session ? m_session->document() : nullptr);
}

void InfoPanel::onSessionDocumentChanged(Ps::ImageDocument *document)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;
    if (m_document) {
        connect(m_document, &Ps::ImageDocument::selectionChanged,
                this, &InfoPanel::refreshSelectionSize);
        connect(m_document, &Ps::ImageDocument::contentChanged,
                this, &InfoPanel::refreshDocumentMemory);
        connect(m_document, &Ps::ImageDocument::structureChanged,
                this, &InfoPanel::refreshDocumentMemory);
    }
    refreshSelectionSize();
    refreshDocumentMemory();
}

void InfoPanel::setCursorInfo(const QPointF &imagePos, bool inside, const QColor &sample)
{
    if (!inside) {
        ui->labelXVal->clear();
        ui->labelYVal->clear();
        clearColorReadout();
        return;
    }

    ui->labelXVal->setText(QString::number(qFloor(imagePos.x())));
    ui->labelYVal->setText(QString::number(qFloor(imagePos.y())));

    if (sample.isValid())
        setColorReadout(sample);
    else
        clearColorReadout();
}

void InfoPanel::setToolHint(const QString &text)
{
    ui->labelToolHint->setText(text);
}

void InfoPanel::refreshSelectionSize()
{
    if (!m_document || m_document->selection().isEmpty()) {
        ui->labelWVal->clear();
        ui->labelHVal->clear();
        return;
    }
    const QRect b = m_document->selection().bounds();
    ui->labelWVal->setText(QString::number(b.width()));
    ui->labelHVal->setText(QString::number(b.height()));
}

void InfoPanel::refreshDocumentMemory()
{
    if (!m_document) {
        ui->labelDocSize->setText(QStringLiteral("文档:—"));
        return;
    }

    // 文档侧：已分配瓦片真实字节 + 选区 mask
    qint64 used = 0;
    const Ps::LayerStack &stack = m_document->layers();
    for (int i = 0; i < stack.count(); ++i) {
        const Ps::Layer *layer = stack.layerAt(i);
        if (!layer)
            continue;
        layer->tiles().forEachAllocatedTile(
            [&](int, int, const QImage &tile, const QRect &) {
                used += qint64(tile.sizeInBytes());
            });
    }
    const QImage &mask = m_document->selection().mask();
    if (!mask.isNull())
        used += qint64(mask.sizeInBytes());

    // 划痕侧：满幅合成缓冲粗估 + 文档占用（对照 PS「aM/bM」）
    const qint64 scratch = qint64(m_document->width()) * m_document->height() * 4 + used;

    ui->labelDocSize->setText(QStringLiteral("文档:%1/%2")
                                  .arg(formatBytesShort(used), formatBytesShort(scratch)));
}

void InfoPanel::clearColorReadout()
{
    ui->labelRVal->clear();
    ui->labelGVal->clear();
    ui->labelBVal->clear();
    ui->labelCVal->clear();
    ui->labelMVal->clear();
    ui->labelCmyYVal->clear();
    ui->labelKVal->clear();
}

void InfoPanel::setColorReadout(const QColor &color)
{
    const int r = color.red();
    const int g = color.green();
    const int b = color.blue();
    ui->labelRVal->setText(QString::number(r));
    ui->labelGVal->setText(QString::number(g));
    ui->labelBVal->setText(QString::number(b));

    int c = 0, m = 0, y = 0, k = 0;
    rgbToCmykPercent(r, g, b, &c, &m, &y, &k);
    ui->labelCVal->setText(QStringLiteral("%1%").arg(c));
    ui->labelMVal->setText(QStringLiteral("%1%").arg(m));
    ui->labelCmyYVal->setText(QStringLiteral("%1%").arg(y));
    ui->labelKVal->setText(QStringLiteral("%1%").arg(k));
}

void InfoPanel::rgbToCmykPercent(int r, int g, int b, int *c, int *m, int *y, int *k)
{
    // 简单 RGB→CMYK（无 ICC）；与常见屏幕预览换算一致
    const qreal rf = r / 255.0;
    const qreal gf = g / 255.0;
    const qreal bf = b / 255.0;
    const qreal kk = 1.0 - qMax(rf, qMax(gf, bf));
    if (kk >= 1.0 - 1e-9) {
        *c = *m = *y = 0;
        *k = 100;
        return;
    }
    *c = qRound((1.0 - rf - kk) / (1.0 - kk) * 100.0);
    *m = qRound((1.0 - gf - kk) / (1.0 - kk) * 100.0);
    *y = qRound((1.0 - bf - kk) / (1.0 - kk) * 100.0);
    *k = qRound(kk * 100.0);
}

QString InfoPanel::formatBytesShort(qint64 bytes)
{
    const double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 0.1)
        return QStringLiteral("%1M").arg(mb, 0, 'f', 1);
    const double kb = bytes / 1024.0;
    if (kb >= 0.1)
        return QStringLiteral("%1K").arg(kb, 0, 'f', 1);
    return QStringLiteral("%1B").arg(bytes);
}
