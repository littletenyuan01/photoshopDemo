/**
 * canvasdocstatusbar.cpp — 画布底栏状态条实现（ui 层）。
 */
#include "canvasdocstatusbar.h"
#include "ui_canvasdocstatusbar.h"

#include "domain/imagedocument.h"

#include <QMenu>

CanvasDocStatusBar::CanvasDocStatusBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CanvasDocStatusBar)
{
    ui->setupUi(this);
    // Action 文案在 .ui；QMenu 不能嵌进带 layout 的 .ui（uic Error 1），故在此挂到按钮
    auto *menu = new QMenu(ui->infoMenuButton);
    menu->addAction(ui->actionInfoDocSize);
    menu->addAction(ui->actionInfoPixelSize);
    ui->infoMenuButton->setMenu(menu);

    connect(ui->actionInfoDocSize, &QAction::triggered,
            this, &CanvasDocStatusBar::onShowDocumentSize);
    connect(ui->actionInfoPixelSize, &QAction::triggered,
            this, &CanvasDocStatusBar::onShowPixelSize);
    connect(ui->zoomEdit, &QLineEdit::editingFinished,
            this, &CanvasDocStatusBar::onZoomEditingFinished);
    setZoomFactor(1.0);
    refreshDocInfo();
}

CanvasDocStatusBar::~CanvasDocStatusBar()
{
    delete ui;
}

void CanvasDocStatusBar::setZoomFactor(qreal zoom)
{
    m_updatingZoomText = true;
    const qreal percent = zoom * 100.0;
    QString text;
    if (qFuzzyCompare(percent, qRound(percent)))
        text = QStringLiteral("%1%").arg(qRound(percent));
    else
        text = QStringLiteral("%1%").arg(percent, 0, 'f', 2);
    ui->zoomEdit->setText(text);
    m_updatingZoomText = false;
}

void CanvasDocStatusBar::setDocument(Ps::ImageDocument *document)
{
    m_document = document;
    refreshDocInfo();
}

void CanvasDocStatusBar::onZoomEditingFinished()
{
    if (m_updatingZoomText)
        return;

    QString t = ui->zoomEdit->text().trimmed();
    t.remove(QLatin1Char('%'));
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const qreal percent = t.toDouble(&ok);
    if (!ok || percent <= 0.0)
        return;
    emit zoomCommitted(percent / 100.0);
}

void CanvasDocStatusBar::onShowDocumentSize()
{
    m_infoMode = InfoMode::DocumentSize;
    refreshDocInfo();
}

void CanvasDocStatusBar::onShowPixelSize()
{
    m_infoMode = InfoMode::PixelSize;
    refreshDocInfo();
}

void CanvasDocStatusBar::refreshDocInfo()
{
    if (!m_document) {
        ui->docInfoLabel->setText(tr("无文档"));
        return;
    }

    const int w = m_document->width();
    const int h = m_document->height();
    if (m_infoMode == InfoMode::PixelSize) {
        ui->docInfoLabel->setText(tr("%1 × %2 像素").arg(w).arg(h));
        return;
    }

    const qreal inchW = w / m_displayPpi;
    const qreal inchH = h / m_displayPpi;
    ui->docInfoLabel->setText(
        tr("%1 × %2 英寸 (%3 PPI)")
            .arg(inchW, 0, 'f', 2)
            .arg(inchH, 0, 'f', 2)
            .arg(qRound(m_displayPpi)));
}
