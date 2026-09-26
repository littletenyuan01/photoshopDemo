#include "colorspanel.h"
#include "ui_colorspanel.h"

#include "hsvcolorwell.h"
#include "itemtreepanel.h"
#include "panelchrome.h"
#include "pixmaputils.h"

#include <QColor>
#include <QFrame>
#include <QIcon>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {

constexpr int kPanelIconSize = 18;
constexpr int kChip = 22;          // PS 色板色块逻辑边长
constexpr int kPresetSquare = 40;  // 渐变/图案方缩略图
constexpr int kDeviceScale = 2;    // 缩略图最高按 2x 光栅化

QString fromUtf8(const char *text)
{
    return QString::fromUtf8(text);
}

QIcon chipIcon(const QColor &color)
{
    return PixmapUtils::multiScaleIcon(
        kChip, kChip, kDeviceScale, [&color](QPainter &painter, const QRect &box) {
            painter.fillRect(box, color);
            painter.setPen(QColor(0x1e, 0x1e, 0x1e));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(0, 0, -1, -1));
        });
}

/** 方块渐变缩略图（对齐 PS 渐变面板，不是宽条）。 */
QIcon squareGradientIcon(const QColor &from, const QColor &to, bool rainbow = false)
{
    return PixmapUtils::multiScaleIcon(
        kPresetSquare, kPresetSquare, kDeviceScale,
        [&from, &to, rainbow](QPainter &painter, const QRect &box) {
            if (to.alpha() < 255)
                PixmapUtils::paintChecker(painter, box, 6, QColor(0xc8, 0xc8, 0xc8),
                                          QColor(0x8a, 0x8a, 0x8a));
            QLinearGradient g(box.topLeft(), box.topRight());
            if (rainbow) {
                for (int i = 0; i <= 6; ++i)
                    g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
            } else {
                g.setColorAt(0.0, from);
                g.setColorAt(1.0, to);
            }
            painter.fillRect(box, g);
            painter.setPen(QColor(0x20, 0x20, 0x20));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(0, 0, -1, -1));
        });
}

QIcon patternIcon(const QString &kind)
{
    return PixmapUtils::multiScaleIcon(
        kPresetSquare, kPresetSquare, kDeviceScale,
        [&kind](QPainter &painter, const QRect &box) {
        painter.fillRect(box, QColor(0x33, 0x33, 0x33));
        const QColor ink(0x9a, 0x9a, 0x9a);
        const int n = kPresetSquare;
        if (kind == fromUtf8("网格")) {
            painter.setPen(ink);
            for (int i = 4; i < n; i += 8) {
                painter.drawLine(i, 0, i, n);
                painter.drawLine(0, i, n, i);
            }
        } else if (kind == fromUtf8("斜纹") || kind == fromUtf8("叶脉")) {
            painter.setPen(ink);
            for (int i = -n; i < n; i += 6)
                painter.drawLine(i, n, i + n, 0);
        } else if (kind == fromUtf8("圆点") || kind == fromUtf8("水珠")) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            for (int y = 5; y < n; y += 9)
                for (int x = 5; x < n; x += 9)
                    painter.drawEllipse(QPointF(x, y), 1.6, 1.6);
        } else if (kind == fromUtf8("棋盘") || kind == fromUtf8("草皮")) {
            constexpr int cell = 8;
            for (int y = 0; y < n; y += cell)
                for (int x = 0; x < n; x += cell)
                    if (((x / cell) + (y / cell)) % 2 == 0)
                        painter.fillRect(QRect(x, y, cell, cell), QColor(0x5a, 0x7a, 0x4a));
                    else
                        painter.fillRect(QRect(x, y, cell, cell), QColor(0x3a, 0x55, 0x32));
        } else if (kind == fromUtf8("横条") || kind == fromUtf8("树皮")) {
            painter.setPen(Qt::NoPen);
            for (int y = 3; y < n; y += 7)
                painter.fillRect(QRect(0, y, n, 3), QColor(0x6a, 0x4a, 0x2a));
        } else if (kind == fromUtf8("竖条") || kind == fromUtf8("枝叶")) {
            painter.setPen(Qt::NoPen);
            for (int x = 3; x < n; x += 7)
                painter.fillRect(QRect(x, 0, 3, n), QColor(0x3a, 0x6a, 0x3a));
        }
        painter.setPen(QColor(0x20, 0x20, 0x20));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.adjusted(0, 0, -1, -1));
    });
}

struct ChipDef {
    const char *name;
    QRgb rgb;
};

struct SwatchGroupDef {
    const char *title;
    const ChipDef *chips;
    int count;
};

const ChipDef kRgbChips[] = {
    {"红", 0xff0000}, {"黄", 0xffff00}, {"绿", 0x00ff00},
    {"青", 0x00ffff}, {"蓝", 0x0000ff}, {"洋红", 0xff00ff},
};
const ChipDef kCmykChips[] = {
    {"青", 0x00a8e8}, {"洋红", 0xe6007e}, {"黄", 0xffed00}, {"黑", 0x1a1a1a},
};
const ChipDef kGrayChips[] = {
    {"10%", 0xe6e6e6}, {"30%", 0xb3b3b3}, {"50%", 0x808080},
    {"70%", 0x4d4d4d}, {"90%", 0x1a1a1a},
};
const ChipDef kPastelChips[] = {
    {"蜡笔黄", 0xffee88}, {"蜡笔蓝", 0x88bbee}, {"蜡笔粉", 0xffb3c9},
    {"蜡笔绿", 0xb8e0b8}, {"蜡笔紫", 0xc9b3ff},
};

struct GradDef {
    const char *name;
    QColor from;
    QColor to;
    bool rainbow = false;
};

struct GradGroupDef {
    const char *title;
    const GradDef *items;
    int count;
};

const GradDef kBasicGrads[] = {
    {"黑白", QColor(0, 0, 0), QColor(255, 255, 255)},
    {"前景到透明", QColor(0, 0, 0), QColor(0, 0, 0, 0)},
    {"白黑", QColor(255, 255, 255), QColor(0, 0, 0)},
};
const GradDef kBlueGrads[] = {
    {"深蓝", QColor(0x0a, 0x1a, 0x40), QColor(0x4a, 0xa0, 0xff)},
    {"青蓝", QColor(0x00, 0x60, 0x80), QColor(0xa0, 0xe8, 0xff)},
};
const GradDef kPurpleGrads[] = {
    {"紫橙", QColor(0x6a, 0x1b, 0x9a), QColor(0xff, 0xa5, 0x00)},
    {"彩虹", QColor(), QColor(), true},
};

struct PatternDef {
    const char *name;
    const char *kind; // 传给 patternIcon 的花纹键
};

struct PatternGroupDef {
    const char *title;
    const PatternDef *items;
    int count;
};

const PatternDef kTreePatterns[] = {
    {"叶脉", "叶脉"}, {"枝叶", "枝叶"}, {"树皮", "树皮"}, {"棋盘叶", "棋盘"},
};
const PatternDef kGrassPatterns[] = {
    {"草皮", "草皮"}, {"斜纹草", "斜纹"},
};
const PatternDef kWaterPatterns[] = {
    {"水珠", "水珠"}, {"网格波", "网格"},
};

void wireFooter(QToolButton *groupBtn, QToolButton *addBtn, QToolButton *delBtn)
{
    ItemTreePanel::applyToolbarIcon(groupBtn, QStringLiteral(":/icons/ui/folder.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(addBtn, QStringLiteral(":/icons/ui/plus.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(delBtn, QStringLiteral(":/icons/layers/delete.svg"), kPanelIconSize);
}

} // namespace

ColorsPanel::ColorsPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ColorsPanel)
{
    ui->setupUi(this);
    PanelChrome::addMenuButton(ui->panelTabs);

    wireFooter(ui->btnSwatchGroup, ui->btnSwatchAdd, ui->btnSwatchDelete);
    wireFooter(ui->btnGradientGroup, ui->btnGradientAdd, ui->btnGradientDelete);
    wireFooter(ui->btnPatternGroup, ui->btnPatternAdd, ui->btnPatternDelete);

    buildSwatchGroups();
    buildGradientGroups();
    buildPatternGroups();

    connect(ui->hsvWell, &HsvColorWell::colorChanged, this, &ColorsPanel::onWellColorChanged);
    connect(ui->hsvWell, &HsvColorWell::swatchesSwapped, this, &ColorsPanel::syncRecentStrip);
    connect(ui->spinR, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->spinG, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->spinB, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->hexEdit, &QLineEdit::textEdited, this, &ColorsPanel::onHexEdited);
    connect(ui->swatchSearch, &QLineEdit::textChanged, this, &ColorsPanel::onSwatchSearchChanged);
    connect(ui->gradientSearch, &QLineEdit::textChanged, this, &ColorsPanel::onGradientSearchChanged);
    connect(ui->patternSearch, &QLineEdit::textChanged, this, &ColorsPanel::onPatternSearchChanged);

    // 用色域当前颜色初始化 RGB/十六进制/最近色条（这一步内部会同步色条，不必再调一次）
    onWellColorChanged(ui->hsvWell->color());
}

ColorsPanel::~ColorsPanel()
{
    delete ui;
}

QListWidget *ColorsPanel::attachChipGrid(QTreeWidget *tree, QTreeWidgetItem *group,
                                         int iconLogical, const QSize &gridCell)
{
    auto *holder = new QTreeWidgetItem(group);
    holder->setFlags(Qt::ItemIsEnabled);
    holder->setText(0, QString());

    auto *list = new QListWidget(tree);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWrapping(true);
    list->setFlow(QListView::LeftToRight);
    list->setIconSize(QSize(iconLogical, iconLogical));
    list->setGridSize(gridCell);
    list->setSpacing(2);
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFocusPolicy(Qt::NoFocus);
    // 高度随内容：先给一行，稍后按 count 调整
    list->setFixedHeight(gridCell.height() + 4);
    tree->setItemWidget(holder, 0, list);
    return list;
}

void ColorsPanel::buildSwatchGroups()
{
    ui->swatchTree->clear();
    const SwatchGroupDef groups[] = {
        {"RGB", kRgbChips, int(sizeof(kRgbChips) / sizeof(kRgbChips[0]))},
        {"CMYK", kCmykChips, int(sizeof(kCmykChips) / sizeof(kCmykChips[0]))},
        {"灰度", kGrayChips, int(sizeof(kGrayChips) / sizeof(kGrayChips[0]))},
        {"蜡笔", kPastelChips, int(sizeof(kPastelChips) / sizeof(kPastelChips[0]))},
    };

    for (const SwatchGroupDef &g : groups) {
        auto *group = new QTreeWidgetItem(ui->swatchTree);
        group->setText(0, fromUtf8(g.title));
        group->setFlags(Qt::ItemIsEnabled);
        QListWidget *grid = attachChipGrid(ui->swatchTree, group, kChip, QSize(kChip + 6, kChip + 6));
        for (int i = 0; i < g.count; ++i) {
            auto *item = new QListWidgetItem(chipIcon(QColor(g.chips[i].rgb)), QString());
            item->setToolTip(fromUtf8(g.chips[i].name));
            item->setData(Qt::UserRole, fromUtf8(g.chips[i].name));
            item->setSizeHint(QSize(kChip + 4, kChip + 4));
            grid->addItem(item);
        }
        // 一行大约放得下 8 块；超出折行加高
        const int cols = qMax(1, (ui->swatchTree->viewport()->width() - 24) / (kChip + 6));
        const int rows = qMax(1, (g.count + cols - 1) / cols);
        grid->setFixedHeight(rows * (kChip + 6) + 6);
    }
    // 默认展开 RGB（对齐截图）
    if (ui->swatchTree->topLevelItemCount() > 0)
        ui->swatchTree->topLevelItem(0)->setExpanded(true);
}

void ColorsPanel::buildGradientGroups()
{
    ui->gradientTree->clear();
    const GradGroupDef groups[] = {
        {"基础", kBasicGrads, int(sizeof(kBasicGrads) / sizeof(kBasicGrads[0]))},
        {"蓝色", kBlueGrads, int(sizeof(kBlueGrads) / sizeof(kBlueGrads[0]))},
        {"紫色", kPurpleGrads, int(sizeof(kPurpleGrads) / sizeof(kPurpleGrads[0]))},
    };

    for (const GradGroupDef &g : groups) {
        auto *group = new QTreeWidgetItem(ui->gradientTree);
        group->setText(0, fromUtf8(g.title));
        group->setFlags(Qt::ItemIsEnabled);
        QListWidget *grid = attachChipGrid(ui->gradientTree, group, kPresetSquare,
                                           QSize(kPresetSquare + 8, kPresetSquare + 8));
        for (int i = 0; i < g.count; ++i) {
            const GradDef &d = g.items[i];
            const QIcon icon = squareGradientIcon(d.from, d.to, d.rainbow);
            auto *item = new QListWidgetItem(icon, QString());
            item->setToolTip(fromUtf8(d.name));
            item->setData(Qt::UserRole, fromUtf8(d.name));
            item->setSizeHint(QSize(kPresetSquare + 6, kPresetSquare + 6));
            grid->addItem(item);
        }
        grid->setFixedHeight(kPresetSquare + 14);
        group->setExpanded(fromUtf8(g.title) == fromUtf8("基础"));
    }
}

void ColorsPanel::buildPatternGroups()
{
    ui->patternTree->clear();
    const PatternGroupDef groups[] = {
        {"树", kTreePatterns, int(sizeof(kTreePatterns) / sizeof(kTreePatterns[0]))},
        {"草", kGrassPatterns, int(sizeof(kGrassPatterns) / sizeof(kGrassPatterns[0]))},
        {"水滴", kWaterPatterns, int(sizeof(kWaterPatterns) / sizeof(kWaterPatterns[0]))},
    };

    for (const PatternGroupDef &g : groups) {
        auto *group = new QTreeWidgetItem(ui->patternTree);
        group->setText(0, fromUtf8(g.title));
        group->setFlags(Qt::ItemIsEnabled);
        QListWidget *grid = attachChipGrid(ui->patternTree, group, kPresetSquare,
                                           QSize(kPresetSquare + 8, kPresetSquare + 8));
        for (int i = 0; i < g.count; ++i) {
            const PatternDef &d = g.items[i];
            auto *item = new QListWidgetItem(patternIcon(fromUtf8(d.kind)), QString());
            item->setToolTip(fromUtf8(d.name));
            item->setData(Qt::UserRole, fromUtf8(d.name));
            item->setSizeHint(QSize(kPresetSquare + 6, kPresetSquare + 6));
            grid->addItem(item);
        }
        const int cols = 4;
        const int rows = qMax(1, (g.count + cols - 1) / cols);
        grid->setFixedHeight(rows * (kPresetSquare + 8) + 6);
        group->setExpanded(fromUtf8(g.title) == fromUtf8("树"));
    }
}

void ColorsPanel::filterPresetTree(QTreeWidget *tree, const QString &needle)
{
    const QString n = needle.trimmed();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = tree->topLevelItem(i);
        bool any = false;
        for (int c = 0; c < group->childCount(); ++c) {
            QTreeWidgetItem *holder = group->child(c);
            auto *list = qobject_cast<QListWidget *>(tree->itemWidget(holder, 0));
            if (!list)
                continue;
            for (int k = 0; k < list->count(); ++k) {
                QListWidgetItem *chip = list->item(k);
                const QString tip = chip->data(Qt::UserRole).toString();
                const bool hit = n.isEmpty() || tip.contains(n, Qt::CaseInsensitive)
                                 || group->text(0).contains(n, Qt::CaseInsensitive);
                chip->setHidden(!hit);
                any = any || hit;
            }
        }
        group->setHidden(!any && !n.isEmpty());
        if (any && !n.isEmpty())
            group->setExpanded(true);
    }
}

void ColorsPanel::onSwatchSearchChanged(const QString &text)
{
    filterPresetTree(ui->swatchTree, text);
}

void ColorsPanel::onGradientSearchChanged(const QString &text)
{
    filterPresetTree(ui->gradientTree, text);
}

void ColorsPanel::onPatternSearchChanged(const QString &text)
{
    filterPresetTree(ui->patternTree, text);
}

void ColorsPanel::onWellColorChanged(const QColor &color)
{
    if (m_syncing)
        return;
    m_syncing = true;
    ui->spinR->setValue(color.red());
    ui->spinG->setValue(color.green());
    ui->spinB->setValue(color.blue());
    ui->hexEdit->setText(color.name(QColor::HexRgb).toUpper());
    m_syncing = false;
    syncRecentStrip();
}

void ColorsPanel::onRgbChanged()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const QColor color(ui->spinR->value(), ui->spinG->value(), ui->spinB->value());
    ui->hexEdit->setText(color.name(QColor::HexRgb).toUpper());
    ui->hsvWell->setColor(color);
    m_syncing = false;
    syncRecentStrip();
}

void ColorsPanel::onHexEdited()
{
    const QColor color = QColor::fromString(ui->hexEdit->text());
    if (!color.isValid())
        return;
    m_syncing = true;
    ui->spinR->setValue(color.red());
    ui->spinG->setValue(color.green());
    ui->spinB->setValue(color.blue());
    ui->hsvWell->setColor(color);
    m_syncing = false;
    syncRecentStrip();
}

void ColorsPanel::syncRecentStrip()
{
    const QString fg = ui->hsvWell->color().name(QColor::HexRgb);
    const QString bg = ui->hsvWell->backgroundColor().name(QColor::HexRgb);
    ui->strip1->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #202020;").arg(fg));
    ui->strip2->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #202020;").arg(bg));
}
