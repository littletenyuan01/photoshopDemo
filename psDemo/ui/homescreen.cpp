#include "homescreen.h"
#include "ui_homescreen.h"

#include "app/recentdocuments.h"

#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr char kPathProp[] = "recentPath";
}

HomeScreen::HomeScreen(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomeScreen)
{
    ui->setupUi(this);
    // recentCard 仅作尺寸/样式模板，真正卡片由 refreshRecent 动态创建
    ui->recentCard->hide();

    m_emptyLabel = new QLabel(tr("暂无最近项目"), ui->contentArea);
    m_emptyLabel->setObjectName(QStringLiteral("recentEmptyLabel"));
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 14px;"));
    m_emptyLabel->hide();
    // 插在 spacer 之前
    ui->cardsLayout->insertWidget(ui->cardsLayout->count() - 1, m_emptyLabel);

    connect(ui->newFileButton, &QPushButton::clicked, this, &HomeScreen::newFileRequested);
    connect(ui->openButton, &QPushButton::clicked, this, &HomeScreen::openFileRequested);
    connect(ui->backButton, &QToolButton::clicked, this, &HomeScreen::backToWorkspaceRequested);

    refreshRecent();
}

HomeScreen::~HomeScreen()
{
    delete ui;
}

void HomeScreen::clearDynamicCards()
{
    // 保留模板 recentCard、emptyLabel、末尾 spacer
    for (int i = ui->cardsLayout->count() - 1; i >= 0; --i) {
        QLayoutItem *item = ui->cardsLayout->itemAt(i);
        QWidget *w = item ? item->widget() : nullptr;
        if (!w || w == ui->recentCard || w == m_emptyLabel)
            continue;
        if (w->property(kPathProp).isValid()) {
            ui->cardsLayout->takeAt(i);
            w->deleteLater();
        }
    }
}

QFrame *HomeScreen::createCard(const QString &path, const QString &title,
                               const QString &subtitle, const QPixmap &thumb)
{
    auto *card = new QFrame(ui->contentArea);
    card->setObjectName(QStringLiteral("recentCard"));
    card->setMinimumSize(ui->recentCard->minimumSize());
    card->setMaximumSize(ui->recentCard->maximumSize());
    card->setFrameShape(QFrame::StyledPanel);
    card->setCursor(Qt::PointingHandCursor);
    card->setToolTip(path);
    card->setProperty(kPathProp, path);
    card->installEventFilter(this);

    auto *layout = new QVBoxLayout(card);
    layout->setSpacing(8);
    layout->setContentsMargins(8, 8, 8, 10);

    auto *thumbLabel = new QLabel(card);
    thumbLabel->setMinimumHeight(130);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setPixmap(thumb.isNull()
                              ? QPixmap(QStringLiteral(":/icons/ui/home-card-thumb.png"))
                              : thumb);
    thumbLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *nameLabel = new QLabel(title, card);
    nameLabel->setObjectName(QStringLiteral("cardName"));
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *timeLabel = new QLabel(subtitle, card);
    timeLabel->setObjectName(QStringLiteral("cardTime"));
    timeLabel->setStyleSheet(QStringLiteral("color: #999; font-size: 11px;"));
    timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(thumbLabel);
    layout->addWidget(nameLabel);
    layout->addWidget(timeLabel);
    return card;
}

void HomeScreen::refreshRecent()
{
    clearDynamicCards();

    const QStringList paths = Ps::RecentDocuments::paths();
    m_emptyLabel->setVisible(paths.isEmpty());

    // 插在 emptyLabel / spacer 之前：模板 hidden 占 index 0，empty 在 spacer 前
    int insertAt = ui->cardsLayout->indexOf(m_emptyLabel);
    if (insertAt < 0)
        insertAt = ui->cardsLayout->count() - 1;

    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString title = info.fileName();
        const QString subtitle = info.lastModified().isValid()
                                     ? info.lastModified().toString(QStringLiteral("yyyy/MM/dd HH:mm"))
                                     : info.absolutePath();

        QPixmap thumb;
        const QImage cached = Ps::RecentDocuments::thumbnail(path);
        if (!cached.isNull())
            thumb = QPixmap::fromImage(cached);

        QFrame *card = createCard(path, title, subtitle, thumb);
        ui->cardsLayout->insertWidget(insertAt++, card);
    }
}

bool HomeScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QVariant pathVar = watched->property(kPathProp);
            if (pathVar.isValid()) {
                const QString path = pathVar.toString();
                if (!path.isEmpty()) {
                    emit recentFileActivated(path);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
