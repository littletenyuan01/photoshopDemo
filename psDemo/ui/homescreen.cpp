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
    // recentCard：.ui 样式/尺寸模板，运行时隐藏；动态卡片从其属性克隆
    ui->recentCard->hide();

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
        if (!w || w == ui->recentCard || w == ui->recentEmptyLabel)
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
    // 布局/尺寸/样式全部来自 .ui 模板 recentCard，这里只克隆并填数据
    auto *tpl = ui->recentCard;
    auto *card = new QFrame(ui->contentArea);
    card->setObjectName(tpl->objectName());
    card->setMinimumSize(tpl->minimumSize());
    card->setMaximumSize(tpl->maximumSize());
    card->setFrameShape(tpl->frameShape());
    card->setCursor(tpl->cursor());
    card->setToolTip(path);
    card->setProperty(kPathProp, path);
    card->installEventFilter(this);

    auto *srcLayout = qobject_cast<QVBoxLayout *>(tpl->layout());
    auto *layout = new QVBoxLayout(card);
    if (srcLayout) {
        layout->setSpacing(srcLayout->spacing());
        layout->setContentsMargins(srcLayout->contentsMargins());
    }

    auto *tplThumb = tpl->findChild<QLabel *>(QStringLiteral("cardThumb"));
    auto *thumbLabel = new QLabel(card);
    thumbLabel->setObjectName(QStringLiteral("cardThumb"));
    if (tplThumb) {
        thumbLabel->setMinimumSize(tplThumb->minimumSize());
        thumbLabel->setMaximumSize(tplThumb->maximumSize());
        thumbLabel->setAlignment(tplThumb->alignment());
        thumbLabel->setScaledContents(tplThumb->hasScaledContents());
    }
    thumbLabel->setPixmap(thumb.isNull()
                              ? QPixmap(QStringLiteral(":/icons/ui/home-card-thumb.png"))
                              : thumb);
    thumbLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *nameLabel = new QLabel(title, card);
    nameLabel->setObjectName(QStringLiteral("cardName"));
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *timeLabel = new QLabel(subtitle, card);
    timeLabel->setObjectName(QStringLiteral("cardTime"));
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
    ui->recentEmptyLabel->setVisible(paths.isEmpty());

    int insertAt = ui->cardsLayout->indexOf(ui->recentEmptyLabel);
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
