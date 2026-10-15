/**
 * historypanel.cpp — 历史面板实现（ui 层）。
 */
#include "historypanel.h"
#include "ui_historypanel.h"

#include "app/appsession.h"
#include "app/historystack.h"
#include "domain/imagedocument.h"

#include <QApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QShortcut>
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

    auto *delShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(delShortcut, &QShortcut::activated, this, &HistoryPanel::onDeleteBookmark);

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
        connect(&hist, &Ps::HistoryStack::bookmarksChanged, this, &HistoryPanel::refreshFromDocument,
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

    // 行 0 = 初始状态（步 0）；行 k = 步 k（显示第 k-1 步条目名 + 可选书签）
    auto rowText = [&](int step, const QString &base) {
        QString label;
        if (hist.bookmarkAt(step, &label))
            return QStringLiteral("%1  [%2]").arg(base, label);
        return base;
    };
    ui->itemList->addItem(rowText(0, tr("初始状态")));
    for (int i = 0; i < n; ++i) {
        const QString name = hist.stepName(i);
        const QString base = name.isEmpty() ? tr("步骤 %1").arg(i + 1) : name;
        ui->itemList->addItem(rowText(i + 1, base));
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
    if (!m_document)
        return;
    Ps::HistoryStack &hist = m_document->history();
    const int step = hist.currentStep();
    QString existing;
    const bool has = hist.bookmarkAt(step, &existing);
    bool ok = false;
    const QString label = QInputDialog::getText(
        this, tr("新建快照"), tr("书签名称："), QLineEdit::Normal,
        has ? existing : tr("快照 %1").arg(hist.bookmarks().size() + 1), &ok);
    if (!ok || label.trimmed().isEmpty())
        return;
    if (!hist.addBookmark(step, label.trimmed())) {
        // 上限或非法步号：静默失败（上限 64）
    }
}

void HistoryPanel::onDeleteBookmark()
{
    if (!m_document)
        return;
    m_document->history().removeBookmark(m_document->history().currentStep());
}
