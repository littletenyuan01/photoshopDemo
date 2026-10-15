/**
 * historypanel.cpp — 历史面板实现（ui 层）。
 */
#include "historypanel.h"
#include "ui_historypanel.h"

#include "app/appsession.h"
#include "app/historystack.h"
#include "domain/imagedocument.h"

#include <QApplication>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QtGlobal>

HistoryPanel::HistoryPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HistoryPanel)
{
    ui->setupUi(this);

    connect(ui->itemList, &QListWidget::currentRowChanged, this, [this](int) {
        onListSelectionChanged();
    });
    connect(ui->btnStepBack, &QToolButton::clicked, this, &HistoryPanel::onStepBack);
    connect(ui->btnStepForward, &QToolButton::clicked, this, &HistoryPanel::onStepForward);
    connect(ui->btnNewSnapshot, &QToolButton::clicked, this, &HistoryPanel::onNewBookmark);

    refreshFromDocument();
}

HistoryPanel::~HistoryPanel()
{
    delete ui;
}

void HistoryPanel::setSession(Ps::AppSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);

    m_session = session;
    if (m_session) {
        connect(m_session, &Ps::AppSession::documentChanged, this,
                [this](Ps::ImageDocument *doc) { bindDocument(doc); });
    }
    bindDocument(m_session ? m_session->document() : nullptr);
}

void HistoryPanel::bindDocument(Ps::ImageDocument *document)
{
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);

    m_document = document;
    if (m_document) {
        Ps::HistoryStack &hist = m_document->history();
        connect(&hist, &Ps::HistoryStack::changed, this, &HistoryPanel::refreshFromDocument,
                Qt::UniqueConnection);
        connect(&hist, &Ps::HistoryStack::historyTrimmed, this, &HistoryPanel::refreshFromDocument,
                Qt::UniqueConnection);
    }
    refreshFromDocument();
}

void HistoryPanel::refreshFromDocument()
{
    m_updating = true;
    const QSignalBlocker blocker(ui->itemList);
    ui->itemList->clear();

    if (!m_document) {
        ui->btnStepBack->setEnabled(false);
        ui->btnStepForward->setEnabled(false);
        ui->btnNewSnapshot->setEnabled(false);
        m_updating = false;
        return;
    }

    Ps::HistoryStack &hist = m_document->history();
    const int n = hist.stepCount();
    const int cur = hist.currentStep();

    // 行 0 = 初始状态；行 k = 到达第 k 步后的状态（显示第 k-1 步的条目名）
    ui->itemList->addItem(tr("初始状态"));
    for (int i = 0; i < n; ++i) {
        const QString name = hist.stepName(i);
        ui->itemList->addItem(name.isEmpty() ? tr("步骤 %1").arg(i + 1) : name);
    }

    if (cur >= 0 && cur <= n)
        ui->itemList->setCurrentRow(cur);

    ui->btnStepBack->setEnabled(hist.canUndo());
    ui->btnStepForward->setEnabled(hist.canRedo());
    ui->btnNewSnapshot->setEnabled(true);
    m_updating = false;
}

void HistoryPanel::jumpWithUiGuard(int step)
{
    if (!m_document)
        return;
    Ps::HistoryStack &hist = m_document->history();
    const int from = hist.currentStep();
    const int target = qBound(0, step, hist.stepCount());
    const int delta = qAbs(target - from);
    const bool longJump = delta >= 10;

    if (longJump) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ui->itemList->setEnabled(false);
    }
    hist.jumpToStep(target, *m_document);
    if (longJump) {
        ui->itemList->setEnabled(true);
        QApplication::restoreOverrideCursor();
    }
    // jumpToStep 会发 history.changed → refreshFromDocument
}

void HistoryPanel::onListSelectionChanged()
{
    if (m_updating || !m_document)
        return;
    const int row = ui->itemList->currentRow();
    if (row < 0)
        return;
    if (row == m_document->history().currentStep())
        return;
    jumpWithUiGuard(row);
}

void HistoryPanel::onStepBack()
{
    if (!m_document)
        return;
    jumpWithUiGuard(m_document->history().currentStep() - 1);
}

void HistoryPanel::onStepForward()
{
    if (!m_document)
        return;
    jumpWithUiGuard(m_document->history().currentStep() + 1);
}

void HistoryPanel::onNewBookmark()
{
    // T5 接线：命名书签
}

void HistoryPanel::onDeleteBookmark()
{
    // T5 接线
}
