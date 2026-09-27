#include "homescreen.h"
#include "ui_homescreen.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>

HomeScreen::HomeScreen(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomeScreen)
{
    ui->setupUi(this);

    // 顶栏应用标：与标题栏同一套 app-logo
    ui->logoLabel->setPixmap(
        QPixmap(QStringLiteral(":/icons/ui/app-logo.png"))
            .scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 主页导航图标（与选项条「家」同源）
    ui->homeNavButton->setIcon(QIcon(QStringLiteral(":/icons/ui/home.png")));
    ui->homeNavButton->setIconSize(QSize(16, 16));

    // 示例缩略：白底 + 小色块，对齐截图里的「未标题」卡片观感
    {
        QPixmap thumb(140, 120);
        thumb.fill(Qt::white);
        QPixmap mark(28, 28);
        mark.fill(QColor(0x8b, 0x1a, 0x1a));
        QPainter p(&thumb);
        p.drawPixmap((thumb.width() - mark.width()) / 2,
                     (thumb.height() - mark.height()) / 2, mark);
        ui->cardThumb->setPixmap(thumb);
    }

    connect(ui->newFileButton, &QPushButton::clicked, this, &HomeScreen::newFileRequested);
    connect(ui->openButton, &QPushButton::clicked, this, &HomeScreen::openFileRequested);
    connect(ui->backButton, &QToolButton::clicked, this, &HomeScreen::backToWorkspaceRequested);
}

HomeScreen::~HomeScreen()
{
    delete ui;
}
