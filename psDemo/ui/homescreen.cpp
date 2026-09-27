#include "homescreen.h"
#include "ui_homescreen.h"

#include <QPushButton>
#include <QToolButton>

HomeScreen::HomeScreen(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::HomeScreen)
{
    ui->setupUi(this);
    // 标志 / 导航图标 / 示例缩略图均在 homescreen.ui

    connect(ui->newFileButton, &QPushButton::clicked, this, &HomeScreen::newFileRequested);
    connect(ui->openButton, &QPushButton::clicked, this, &HomeScreen::openFileRequested);
    connect(ui->backButton, &QToolButton::clicked, this, &HomeScreen::backToWorkspaceRequested);
}

HomeScreen::~HomeScreen()
{
    delete ui;
}
