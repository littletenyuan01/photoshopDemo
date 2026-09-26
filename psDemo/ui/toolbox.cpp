#include "toolbox.h"
#include "ui_toolbox.h"
#include "colorpickerdialog.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPolygonF>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/** 工具箱按钮的图标边长（逻辑像素）。 */
constexpr int kToolIconSize = 24;

/**
 * 由源图生成「多档 DPR、按设备像素 1:1 渲染」的 QIcon；可选右下角小三角（同组标记）。
 *
 * 【坑 · 实测过】早先写法是：
 *     QPixmap pm(20,20);
 *     p.drawPixmap(0, 0, base.pixmap(QSize(20,20)));   // 先缩到 20×20
 *     return QIcon(pm);                                 // 只含一张 20×20、DPR=1 的位图
 * 屏幕缩放（如 150%/300%）时 Qt 需要 30/60 像素的图，而 QIcon 里只有 20×20，
 * **只能放大小位图 → 图标发虚、带灰晕**（用户截图看到的"糊糊的"）。
 *
 * 正确做法：对每档 DPR 都从**源图直接缩放到设备像素尺寸**（都是降采样，因此锐利），
 * 再 `setDevicePixelRatio` 后加入同一个 QIcon，由 Qt 按屏幕 DPR 取用最合适的那张。
 */
QIcon toolIcon(const QIcon &base, const QSize &logicalSize, bool groupMark)
{
    QIcon out;
    const int scales[] = {1, 2, 3};
    for (int s : scales) {
        const QSize device(logicalSize.width() * s, logicalSize.height() * s);
        QPixmap pm(device);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(0, 0, base.pixmap(device));   // 源图 → 设备尺寸，只缩一次

        if (groupMark) {
            // 小三角也随 DPR 一起放大，否则在高分屏上会细到看不见
            const qreal side = 5.0 * s;
            QPolygonF tri;
            tri << QPointF(device.width() - 1, device.height() - side - 1)
                << QPointF(device.width() - 1, device.height() - 1)
                << QPointF(device.width() - side - 1, device.height() - 1);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(230, 230, 230));
            p.drawPolygon(tri);
        }
        p.end();

        pm.setDevicePixelRatio(s);
        out.addPixmap(pm);
    }
    return out;
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
    const QSize sz(kToolIconSize, kToolIconSize);
    // 关键：所有图标都走多档 DPR 生成（不论是否同组），否则屏幕缩放时会发虚
    QIcon icon = toolIcon(loadIcon(item.iconPath), sz, slot.items.size() > 1);

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
    // 分组与顺序对齐 Photoshop 默认工具箱：同组共用一个占位，右键展开子菜单。
    // ⚠️ 只有 Move / Hand / Zoom / Brush / Eraser 有实际逻辑，其余是 UI 占位
    //    （选中后 ToolManager 回退到中性工具，不消费事件）。
    const QString dir = QStringLiteral(":/icons/tools/");
    const auto icon = [&dir](const char *name) {
        return dir + QString::fromLatin1(name) + QStringLiteral(".png");
    };

    // 移动（V）
    addSlot({{Ps::ToolId::Move, icon("move"), tr("移动工具"), QStringLiteral("V")}});

    // 选框（M）
    addSlot({{Ps::ToolId::RectSelect, icon("rect-select"), tr("矩形选框工具"), QStringLiteral("M")},
             {Ps::ToolId::EllipseSelect, icon("ellipse-select"), tr("椭圆选框工具"), QStringLiteral("M")}});

    // 套索（L）
    addSlot({{Ps::ToolId::Lasso, icon("lasso"), tr("套索工具"), QStringLiteral("L")},
             {Ps::ToolId::PolygonalLasso, icon("lasso-alt"), tr("多边形套索工具"), QStringLiteral("L")},
             {Ps::ToolId::MagneticLasso, icon("magnetic-lasso"), tr("磁性套索工具"), QStringLiteral("L")}});

    // 快速选择（W）
    addSlot({{Ps::ToolId::QuickSelect, icon("quick-select"), tr("快速选择工具"), QStringLiteral("W")},
             {Ps::ToolId::MagicWand, icon("magic-wand"), tr("魔棒工具"), QStringLiteral("W")}});

    // 裁剪（C）
    addSlot({{Ps::ToolId::Crop, icon("crop"), tr("裁剪工具"), QStringLiteral("C")},
             {Ps::ToolId::PerspectiveCrop, icon("perspective"), tr("透视裁剪工具"), QStringLiteral("C")}});

    // 吸管（I）
    addSlot({{Ps::ToolId::Eyedropper, icon("eyedropper"), tr("吸管工具"), QStringLiteral("I")}});

    // 画笔（B）
    addSlot({{Ps::ToolId::Brush, icon("brush"), tr("画笔工具"), QStringLiteral("B")},
             {Ps::ToolId::Pencil, icon("pencil"), tr("铅笔工具"), QStringLiteral("B")},
             {Ps::ToolId::MixerBrush, icon("brush-pencil"), tr("混合器画笔工具"), QStringLiteral("B")}});

    // 图章（S）
    addSlot({{Ps::ToolId::CloneStamp, icon("stamp"), tr("仿制图章工具"), QStringLiteral("S")}});

    // 橡皮擦（E）
    addSlot({{Ps::ToolId::Eraser, icon("eraser"), tr("橡皮擦工具"), QStringLiteral("E")},
             {Ps::ToolId::BackgroundEraser, icon("eraser-alt"),
              tr("背景橡皮擦工具"), QStringLiteral("E")}});

    // 填充（G）
    addSlot({{Ps::ToolId::PaintBucket, icon("bucket"), tr("油漆桶工具"), QStringLiteral("G")},
             {Ps::ToolId::Gradient, icon("gradient"), tr("渐变工具"), QStringLiteral("G")}});

    // 聚焦：模糊 / 锐化 / 涂抹
    addSlot({{Ps::ToolId::Blur, icon("blur"), tr("模糊工具"), QString()},
             {Ps::ToolId::Sharpen, icon("sharpen"), tr("锐化工具"), QString()},
             {Ps::ToolId::Smudge, icon("smudge"), tr("涂抹工具"), QString()}});

    // 色调：减淡 / 海绵
    addSlot({{Ps::ToolId::Dodge, icon("adjust-add"), tr("减淡工具"), QString()},
             {Ps::ToolId::Sponge, icon("sponge"), tr("海绵工具"), QString()}});

    // 钢笔（P）
    addSlot({{Ps::ToolId::Pen, icon("pen"), tr("钢笔工具"), QStringLiteral("P")},
             {Ps::ToolId::FreeformPen, icon("pen-alt"), tr("自由钢笔工具"), QStringLiteral("P")},
             {Ps::ToolId::AddAnchorPoint, icon("pen-add"), tr("添加锚点工具"), QStringLiteral("P")}});

    // 文字（T）
    addSlot({{Ps::ToolId::Type, icon("type-horizontal"), tr("横排文字工具"), QStringLiteral("T")},
             {Ps::ToolId::TypeVertical, icon("type-vertical"), tr("直排文字工具"), QStringLiteral("T")}});

    // 形状（U）
    addSlot({{Ps::ToolId::ShapeRect, icon("rectangle"), tr("矩形工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeEllipse, icon("ellipse"), tr("椭圆工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeTriangle, icon("triangle"), tr("三角形工具"), QStringLiteral("U")},
             {Ps::ToolId::ShapeLine, icon("line"), tr("直线工具"), QStringLiteral("U")}});

    // 视图
    addSlot({{Ps::ToolId::Hand, icon("hand"), tr("抓手工具"), QStringLiteral("H")}});
    addSlot({{Ps::ToolId::Zoom, icon("zoom"), tr("缩放工具"), QStringLiteral("Z")}});
}
