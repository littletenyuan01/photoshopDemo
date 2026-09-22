#include "canvasdocstatusbar.h"
#include "ui_canvasdocstatusbar.h"

#include "domain/imagedocument.h"

#include <QMenu>

CanvasDocStatusBar::CanvasDocStatusBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CanvasDocStatusBar)
{
    ui->setupUi(this);
    setupInfoMenu();
    connect(ui->zoomEdit, &QLineEdit::editingFinished,
            this, &CanvasDocStatusBar::onZoomEditingFinished);
    connect(ui->zoomEdit, &QLineEdit::returnPressed,
            this, &CanvasDocStatusBar::onZoomEditingFinished);
    setZoomFactor(1.0);
    refreshDocInfo();
}

CanvasDocStatusBar::~CanvasDocStatusBar()
{
    delete ui;
}

void CanvasDocStatusBar::setupInfoMenu()
{
    auto *menu = new QMenu(this);
    QAction *docAct = menu->addAction(tr("文档大小"));
    QAction *pxAct = menu->addAction(tr("文档大小（像素）"));
    connect(docAct, &QAction::triggered, this, &CanvasDocStatusBar::onShowDocumentSize);
    connect(pxAct, &QAction::triggered, this, &CanvasDocStatusBar::onShowPixelSize);
    ui->infoMenuButton->setMenu(menu);
}

void CanvasDocStatusBar::setZoomFactor(qreal zoom)
{
    m_updatingZoomText = true;
    // 对齐 PS：常见为两位小数百分比，整百则可不写多余 0
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
    if (!ok || percent <= 0.0) {
        // 非法输入：恢复为当前不触发 commit（由外部下次 sync）
        return;
    }
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

    // 像素 → 厘米：cm = px / ppi * 2.54（显示用，文档模型暂无独立分辨率字段）
    const qreal cmW = w / m_displayPpi * 2.54;
    const qreal cmH = h / m_displayPpi * 2.54;
    ui->docInfoLabel->setText(
        tr("%1 厘米 × %2 厘米 (%3 ppi)")
            .arg(cmW, 0, 'f', 2)
            .arg(cmH, 0, 'f', 2)
            .arg(qRound(m_displayPpi)));
}
