#include "toolbox.h"
#include "ui_toolbox.h"
#include "colorpickerdialog.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/** 在图标右下角画小三角，提示该占位有多个子工具（对齐 PS）。 */
QIcon withGroupMark(const QIcon &base, const QSize &size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPixmap src = base.pixmap(size);
    p.drawPixmap(0, 0, src);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(230, 230, 230));
    const int s = 5;
    QPolygon tri;
    tri << QPoint(size.width() - 1, size.height() - s - 1)
        << QPoint(size.width() - 1, size.height() - 1)
        << QPoint(size.width() - s - 1, size.height() - 1);
    p.drawPolygon(tri);
    p.end();
    return QIcon(pm);
}

} // namespace

ToolBox::ToolBox(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolBox)
{
    ui->setupUi(this);
    ui->toolsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: #5a5a5a; border: none; }"));
    ui->toolsHost->setStyleSheet(QStringLiteral("background-color: #5a5a5a;"));
    ui->toolsScroll->viewport()->setStyleSheet(QStringLiteral("background-color: #5a5a5a;"));

    m_slotGroup = new QButtonGroup(this);
    m_slotGroup->setExclusive(true);
    connect(m_slotGroup, &QButtonGroup::idClicked, this, &ToolBox::onSlotClicked);

    buildToolSlots();
    updateColorButtons();
    ui->fgButton->raise();

    connect(ui->fgButton, &QPushButton::clicked, this, &ToolBox::onPickForeground);
    connect(ui->bgButton, &QPushButton::clicked, this, &ToolBox::onPickBackground);
    connect(ui->swapColorsButton, &QToolButton::clicked, this, &ToolBox::onSwapColors);
    connect(ui->defaultColorsButton, &QToolButton::clicked, this, &ToolBox::onDefaultColors);

    setCurrentTool(Ps::ToolId::Move);
}

ToolBox::~ToolBox()
{
    delete ui;
}

void ToolBox::setCurrentTool(Ps::ToolId id)
{
    const int slot = findSlotIndex(id);
    if (slot < 0)
        return;

    // 若该工具在组内，切换组内 activeIndex
    ToolSlot &s = m_slots[slot];
    for (int i = 0; i < s.items.size(); ++i) {
        if (s.items[i].id == id) {
            s.activeIndex = i;
            break;
        }
    }
    refreshSlotButton(slot);
    if (auto *btn = m_slotGroup->button(slot))
        btn->setChecked(true);

    if (m_currentTool == id)
        return;
    m_currentTool = id;
    emit toolChanged(id);
}

void ToolBox::onSlotClicked(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= m_slots.size())
        return;
    const ToolItem &item = m_slots[slotIndex].items[m_slots[slotIndex].activeIndex];
    if (m_currentTool == item.id)
        return;
    m_currentTool = item.id;
    emit toolChanged(item.id);
}

void ToolBox::onPickForeground()
{
    const QColor c = ColorPickerDialog::getColor(
        m_fg, this, ColorPickerDialog::Mode::Foreground);
    if (!c.isValid())
        return;
    m_fg = c;
    updateColorButtons();
    emit foregroundColorChanged(m_fg);
}

void ToolBox::onPickBackground()
{
    const QColor c = ColorPickerDialog::getColor(
        m_bg, this, ColorPickerDialog::Mode::Background);
    if (!c.isValid())
        return;
    m_bg = c;
    updateColorButtons();
    emit backgroundColorChanged(m_bg);
}

void ToolBox::onSwapColors()
{
    qSwap(m_fg, m_bg);
    updateColorButtons();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}

void ToolBox::onDefaultColors()
{
    m_fg = Qt::black;
    m_bg = Qt::white;
    updateColorButtons();
    emit foregroundColorChanged(m_fg);
    emit backgroundColorChanged(m_bg);
}

void ToolBox::updateColorButtons()
{
    // 前景始终画在上层、浅色粗边；背景在下层右下角 —— 交换只改 fill，不改前后位置
    ui->fgButton->setStyleSheet(
        QStringLiteral("QPushButton#fgButton {"
                       " background-color: %1;"
                       " border: 2px solid #f0f0f0;"
                       " border-radius: 1px;"
                       "}")
            .arg(m_fg.name()));
    ui->bgButton->setStyleSheet(
        QStringLiteral("QPushButton#bgButton {"
                       " background-color: %1;"
                       " border: 1px solid #111;"
                       " border-radius: 1px;"
                       "}")
            .arg(m_bg.name()));
    ui->fgButton->raise();
    ui->fgButton->setToolTip(tr("前景色（画笔等使用）\n当前：%1").arg(m_fg.name()));
    ui->bgButton->setToolTip(tr("背景色\n当前：%1").arg(m_bg.name()));
}

QIcon ToolBox::loadIcon(const QString &path) const
{
    return QIcon(path);
}

int ToolBox::findSlotIndex(Ps::ToolId id) const
{
    for (int i = 0; i < m_slots.size(); ++i) {
        for (const ToolItem &item : m_slots[i].items) {
            if (item.id == id)
                return i;
        }
    }
    return -1;
}

void ToolBox::refreshSlotButton(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= m_slots.size())
        return;
    ToolSlot &slot = m_slots[slotIndex];
    if (!slot.button || slot.items.isEmpty())
        return;

    const ToolItem &item = slot.items[slot.activeIndex];
    QIcon icon = loadIcon(item.iconPath);
    const QSize sz(20, 20);
    if (slot.items.size() > 1)
        icon = withGroupMark(icon, sz);

    slot.button->setIcon(icon);
    slot.button->setIconSize(sz);
    slot.button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    slot.button->setToolTip(item.title
                            + (item.shortcut.isEmpty()
                                   ? QString()
                                   : QStringLiteral(" (%1)").arg(item.shortcut))
                            + (slot.items.size() > 1
                                   ? tr("\n右键选择同组工具")
                                   : QString()));
}

void ToolBox::showSlotMenu(int slotIndex, const QPoint &globalPos)
{
    if (slotIndex < 0 || slotIndex >= m_slots.size())
        return;
    ToolSlot &slot = m_slots[slotIndex];
    if (slot.items.size() <= 1)
        return;

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #3c3c3c; color: #eee; border: 1px solid #222; }"
        "QMenu::item:selected { background-color: #2d5a8a; }"
        "QMenu::icon { padding-left: 4px; }"));

    for (int i = 0; i < slot.items.size(); ++i) {
        const ToolItem &item = slot.items[i];
        QAction *act = menu.addAction(loadIcon(item.iconPath), item.title);
        act->setData(i);
        if (!item.shortcut.isEmpty())
            act->setShortcut(QKeySequence(item.shortcut));
        if (i == slot.activeIndex) {
            act->setCheckable(true);
            act->setChecked(true);
        }
    }

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    const int idx = chosen->data().toInt();
    if (idx < 0 || idx >= slot.items.size())
        return;

    slot.activeIndex = idx;
    refreshSlotButton(slotIndex);
    if (auto *btn = m_slotGroup->button(slotIndex))
        btn->setChecked(true);

    const Ps::ToolId id = slot.items[idx].id;
    if (m_currentTool != id) {
        m_currentTool = id;
        emit toolChanged(id);
    }
}

void ToolBox::addSlot(const QVector<ToolItem> &items)
{
    if (items.isEmpty())
        return;

    const int slotIndex = m_slots.size();
    ToolSlot slot;
    slot.items = items;
    slot.activeIndex = 0;

    auto *btn = new QToolButton(ui->toolsHost);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setFixedSize(36, 32);
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    // 右键弹出同组工具（Photoshop 飞出菜单行为）
    connect(btn, &QWidget::customContextMenuRequested, this,
            [this, slotIndex, btn](const QPoint &pos) {
                showSlotMenu(slotIndex, btn->mapToGlobal(pos));
            });

    QVBoxLayout *layout = ui->toolsLayout;
    layout->insertWidget(qMax(0, layout->count() - 1), btn);

    m_slotGroup->addButton(btn, slotIndex);
    slot.button = btn;
    m_slots.push_back(slot);
    refreshSlotButton(slotIndex);
}

void ToolBox::buildToolSlots()
{
    // 图标：resources/icons/tools/*.png（iconfont，英文文件名）
    const QString dir = QStringLiteral(":/icons/tools/");

    addSlot({{Ps::ToolId::Move, dir + QStringLiteral("move.png"),
              tr("移动工具"), QStringLiteral("V")}});

    // 选框组
    addSlot({{Ps::ToolId::RectSelect, dir + QStringLiteral("rect-select.png"),
              tr("矩形选框工具"), QStringLiteral("M")},
             {Ps::ToolId::EllipseSelect, dir + QStringLiteral("ellipse-select.png"),
              tr("椭圆选框工具"), QStringLiteral("M")}});

    addSlot({{Ps::ToolId::Lasso, dir + QStringLiteral("lasso.png"),
              tr("套索工具"), QStringLiteral("L")}});

    addSlot({{Ps::ToolId::MagicWand, dir + QStringLiteral("magic-wand.png"),
              tr("魔棒工具"), QStringLiteral("W")}});

    addSlot({{Ps::ToolId::Crop, dir + QStringLiteral("crop.png"),
              tr("裁剪工具"), QStringLiteral("C")}});

    addSlot({{Ps::ToolId::Eyedropper, dir + QStringLiteral("eyedropper.png"),
              tr("吸管工具"), QStringLiteral("I")}});

    addSlot({{Ps::ToolId::Brush, dir + QStringLiteral("brush.png"),
              tr("画笔工具"), QStringLiteral("B")}});

    addSlot({{Ps::ToolId::Eraser, dir + QStringLiteral("eraser.png"),
              tr("橡皮擦工具"), QStringLiteral("E")}});

    // 填充组：油漆桶 + 渐变
    addSlot({{Ps::ToolId::PaintBucket, dir + QStringLiteral("bucket.png"),
              tr("油漆桶工具"), QStringLiteral("G")},
             {Ps::ToolId::Gradient, dir + QStringLiteral("gradient.png"),
              tr("渐变工具"), QStringLiteral("G")}});

    addSlot({{Ps::ToolId::Type, dir + QStringLiteral("type-horizontal.png"),
              tr("横排文字工具"), QStringLiteral("T")}});

    // 形状组：矩形 / 椭圆 / 三角 / 直线 — 同一占位，右键展开（对齐用户 PS 截图）
    addSlot({{Ps::ToolId::ShapeRect, dir + QStringLiteral("rectangle.png"),
              tr("矩形工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeEllipse, dir + QStringLiteral("ellipse.png"),
              tr("椭圆工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeTriangle, dir + QStringLiteral("triangle.png"),
              tr("三角形工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeLine, dir + QStringLiteral("line.png"),
              tr("直线工具"), QStringLiteral("U")}});

    addSlot({{Ps::ToolId::Hand, dir + QStringLiteral("hand.png"),
              tr("抓手工具"), QStringLiteral("H")}});

    addSlot({{Ps::ToolId::Zoom, dir + QStringLiteral("zoom.png"),
              tr("缩放工具"), QStringLiteral("Z")}});
}
