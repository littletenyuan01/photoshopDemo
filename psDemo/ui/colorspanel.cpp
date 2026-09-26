#include "colorspanel.h"
#include "ui_colorspanel.h"

#include "itemtreepanel.h"
#include "panelchrome.h"

#include <QColor>
#include <QIcon>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidgetItemIterator>

namespace {

/** 面板内小按钮的图标边长（比列表底栏的 24px 小一号，适配本面板较矮的底栏）。 */
constexpr int kPanelIconSize = 18;

/** 色板色块缩略图边长（逻辑像素）。 */
constexpr int kChipSize = 14;

/** 渐变条缩略图尺寸（逻辑像素）。 */
constexpr int kGradientW = 60;
constexpr int kGradientH = 14;

/** 图案缩略图边长（逻辑像素）。 */
constexpr int kPatternSize = 40;

/** 下采样到 2x 设备像素再打 DPR：HiDPI 下渐变/图案才不会发糊。 */
constexpr int kDeviceScale = 2;

/**
 * 源码里的中文字面量是 **UTF-8 字节**，比较前必须显式解码。
 * 【坑】曾经写成 `item->text(0) != QLatin1String("纯红")`：QLatin1String 把 UTF-8
 * 字节当 Latin-1，永远比不中，于是色板一个色块都不显示、渐变一个缩略图都不画、
 * 图案全画成同一个默认方块 —— 而且不报任何错。
 */
QString fromUtf8(const char *text)
{
    return QString::fromUtf8(text);
}

/**
 * 造一张「逻辑尺寸 w×h、按 scale 倍光栅化」的透明画布，返回时已打好 DPR。
 * 【坑】不要先给 QPixmap 设 DPR 再按设备像素画（会画两遍）；
 * 手工绘制时坐标一律用逻辑像素，DPI 缩放交给 QPainter 处理。
 * 【对照】ItemTreePanel::svgIcon 对 SVG 走的是同一条思路。
 */
QPixmap canvas(int w, int h, int scale)
{
    QPixmap pm(w * scale, h * scale);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(scale);
    return pm;
}

/**
 * 用同一段绘制代码产出 1x / 2x 两档图标（同 ItemTreePanel::svgIcon 的做法）。
 * @param paint 形如 void(QPainter &, const QRect &logicalBox)
 */
template <typename Paint>
QIcon twoScaleIcon(int w, int h, Paint paint)
{
    QIcon icon;
    for (int scale = 1; scale <= kDeviceScale; ++scale) {
        QPixmap pm = canvas(w, h, scale);
        QPainter painter(&pm);
        paint(painter, QRect(0, 0, w, h));
        painter.end();
        icon.addPixmap(pm);
    }
    return icon;
}

/** 透明棋盘衬底（渐变条里表示「透明」那一端）。 */
void paintChecker(QPainter &painter, const QRect &rect, int cell)
{
    painter.fillRect(rect, QColor(0xc8, 0xc8, 0xc8));
    for (int y = 0; y < rect.height(); y += cell) {
        for (int x = 0; x < rect.width(); x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                continue;
            painter.fillRect(rect.x() + x, rect.y() + y, cell, cell, QColor(0x8a, 0x8a, 0x8a));
        }
    }
}

/** 色块缩略图：实色 + 深色描边（PS 色板里每个色块的样子）。 */
QIcon chipIcon(const QColor &color)
{
    return twoScaleIcon(kChipSize, kChipSize, [&color](QPainter &painter, const QRect &box) {
        painter.fillRect(box, color);
        painter.setPen(QColor(0x1e, 0x1e, 0x1e));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.adjusted(0, 0, -1, -1));
    });
}

/** 一行色板的占位定义：显示名 → 颜色。 */
struct SwatchEntry {
    const char *name;
    QRgb rgb;
};

/**
 * 【UI 占位】色板树里的色名与色值。真实产品应从 .gpl 调色板文件读
 * （GIMP 走 GimpPalette 的 GimpData 载入机制），本阶段只做外观。
 */
const SwatchEntry kSwatches[] = {
    {"纯红", 0xff0000},   {"纯绿", 0x00ff00},   {"纯蓝", 0x0000ff},
    {"青", 0x00ffff},     {"洋红", 0xff00ff},   {"黄", 0xffff00},
    {"10% 灰", 0x1a1a1a}, {"50% 灰", 0x808080}, {"蜡笔黄", 0xffee88},
    {"蜡笔蓝", 0x88bbee}, {"浅粉", 0xffd9e0},   {"浅青", 0xd9f2f2},
};

/** 一行渐变预设的占位定义：显示名 → 起止色（末位可带 alpha，用于「到透明」）。 */
struct GradientEntry {
    const char *name;
    QColor from;
    QColor to;
};

/** 【UI 占位】渐变预设；真实产品应读 .ggr 渐变文件（GimpGradient）。 */
const GradientEntry kGradients[] = {
    {"前景色到背景色", QColor(0x00, 0x00, 0x00), QColor(0xff, 0xff, 0xff)},
    {"前景色到透明", QColor(0x00, 0x00, 0x00), QColor(0x00, 0x00, 0x00, 0x00)},
    {"黑色到白色", QColor(0x00, 0x00, 0x00), QColor(0xff, 0xff, 0xff)},
    {"铜色", QColor(0x4a, 0x24, 0x0c), QColor(0xf0, 0xc8, 0x78)},
    {"紫到橙", QColor(0x6a, 0x1b, 0x9a), QColor(0xff, 0xa5, 0x00)},
};

/**
 * 渐变条缩略图；「彩虹」按色相环生成，其余取占位表。
 * 【返回空 QIcon】名字既不在表里也不是「彩虹」时返回空图标：留空比默默画成纯黑
 * 更容易发现走样（.ui 里的预设名与占位表对不上时，一眼能看出来）。
 */
QIcon gradientThumb(const QString &name)
{
    QLinearGradient gradient(QPointF(0, 0), QPointF(kGradientW, 0));
    bool transparentEnd = false;
    bool found = false;
    if (name == fromUtf8("彩虹")) {
        for (int i = 0; i <= 6; ++i)
            gradient.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        found = true;
    } else {
        for (const GradientEntry &entry : kGradients) {
            if (name != fromUtf8(entry.name))
                continue;
            gradient.setColorAt(0.0, entry.from);
            gradient.setColorAt(1.0, entry.to);
            transparentEnd = entry.to.alpha() < 255;
            found = true;
            break;
        }
    }
    if (!found)
        return QIcon();

    return twoScaleIcon(kGradientW, kGradientH,
                        [&gradient, transparentEnd](QPainter &painter, const QRect &box) {
                            if (transparentEnd)
                                paintChecker(painter, box, 7);
                            painter.fillRect(box, gradient);
                            painter.setPen(QColor(0x20, 0x20, 0x20));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(box.adjusted(0, 0, -1, -1));
                        });
}

/** 图案缩略图；几何全是现画的占位花纹。 */
QIcon patternThumb(const QString &kind)
{
    return twoScaleIcon(kPatternSize, kPatternSize, [&kind](QPainter &painter, const QRect &box) {
        painter.fillRect(box, QColor(0x33, 0x33, 0x33));

        const QColor ink(0x9a, 0x9a, 0x9a);
        if (kind == fromUtf8("网格")) {
            painter.setPen(ink);
            for (int i = 4; i < kPatternSize; i += 8) {
                painter.drawLine(i, 0, i, kPatternSize);
                painter.drawLine(0, i, kPatternSize, i);
            }
        } else if (kind == fromUtf8("斜纹")) {
            painter.setPen(ink);
            for (int i = -kPatternSize; i < kPatternSize; i += 6)
                painter.drawLine(i, kPatternSize, i + kPatternSize, 0);
        } else if (kind == fromUtf8("圆点")) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            for (int y = 5; y < kPatternSize; y += 9)
                for (int x = 5; x < kPatternSize; x += 9)
                    painter.drawEllipse(QPointF(x, y), 1.6, 1.6);
        } else if (kind == fromUtf8("棋盘")) {
            painter.setPen(Qt::NoPen);
            constexpr int cell = 8;
            for (int y = 0; y < kPatternSize; y += cell)
                for (int x = 0; x < kPatternSize; x += cell)
                    if (((x / cell) + (y / cell)) % 2 == 0)
                        painter.fillRect(QRect(x, y, cell, cell), QColor(0xc8, 0xc8, 0xc8));
        } else if (kind == fromUtf8("横条")) {
            painter.setPen(Qt::NoPen);
            for (int y = 3; y < kPatternSize; y += 8)
                painter.fillRect(QRect(0, y, kPatternSize, 2), ink);
        } else if (kind == fromUtf8("竖条")) {
            painter.setPen(Qt::NoPen);
            for (int x = 3; x < kPatternSize; x += 8)
                painter.fillRect(QRect(x, 0, 2, kPatternSize), ink);
        }

        painter.setPen(QColor(0x20, 0x20, 0x20));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(box.adjusted(0, 0, -1, -1));
    });
}

} // namespace

ColorsPanel::ColorsPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ColorsPanel)
{
    ui->setupUi(this);

    PanelChrome::addMenuButton(ui->panelTabs);

    // 底栏三个动作按钮：复用 ItemTreePanel 的 SVG 光栅化（18px 比列表底栏的 24px 小一号）
    ItemTreePanel::applyToolbarIcon(ui->btnSwatchGroup,
                                    QStringLiteral(":/icons/ui/plus.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnSwatchAdd,
                                    QStringLiteral(":/icons/layers/new-layer.svg"), kPanelIconSize);
    ItemTreePanel::applyToolbarIcon(ui->btnSwatchDelete,
                                    QStringLiteral(":/icons/layers/delete.svg"), kPanelIconSize);

    buildSwatchChips();
    buildGradientThumbs();
    buildPatternThumbs();

    // PS 的色板面板一眼就能看到色块，故默认把色板组全部展开
    // （折叠着只剩 5 行组名，等于把这块面板做成空白）
    ui->swatchTree->expandAll();

    connect(ui->spinR, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->spinG, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->spinB, &QSpinBox::valueChanged, this, &ColorsPanel::onRgbChanged);
    connect(ui->hexEdit, &QLineEdit::textEdited, this, &ColorsPanel::onHexEdited);
    connect(ui->hueSlider, &QSlider::valueChanged, this, &ColorsPanel::onHueChanged);
    connect(ui->swatchSearch, &QLineEdit::textChanged, this, &ColorsPanel::onSwatchSearchChanged);

    // 让「16 进制文本 / 前景色块」在构造完成时就与 RGB 微调框一致，
    // 而不是靠 .ui 里的硬编码初值碰巧一致。
    onRgbChanged();
}

ColorsPanel::~ColorsPanel()
{
    delete ui;
}

void ColorsPanel::onRgbChanged()
{
    const QColor color(ui->spinR->value(), ui->spinG->value(), ui->spinB->value());
    // setText() 只发 textChanged，不发 textEdited，所以这里不会绕回 onHexEdited
    ui->hexEdit->setText(color.name(QColor::HexRgb).toUpper());
    syncForegroundSwatch();
}

void ColorsPanel::onHexEdited()
{
    const QColor color = QColor::fromString(ui->hexEdit->text());
    if (!color.isValid())
        return; // 打到一半的非法输入：保留原值，不报错、不抖动
    ui->spinR->setValue(color.red());
    ui->spinG->setValue(color.green());
    ui->spinB->setValue(color.blue());
}

void ColorsPanel::onHueChanged(int hue)
{
    // 色域方块用「纯色相 → 白」的竖直渐变近似 PS 的 HSV 方块（样式表做不出二维渐变）
    const QColor pure = QColor::fromHsv(hue, 255, 255);
    ui->colorField->setStyleSheet(
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                       " stop:0 %1, stop:1 #ffffff);"
                       "border: 1px solid #202020;")
            .arg(pure.name()));

    // 同步到 RGB 三个框；后续联动由 onRgbChanged 负责
    ui->spinR->setValue(pure.red());
    ui->spinG->setValue(pure.green());
    ui->spinB->setValue(pure.blue());
}

void ColorsPanel::onSwatchSearchChanged(const QString &text)
{
    const QString needle = text.trimmed();
    QTreeWidgetItemIterator it(ui->swatchTree);
    for (; *it; ++it) {
        QTreeWidgetItem *item = *it;
        if (item->childCount() > 0) {
            // 分组行先跟着子项显隐，稍后由子项命中结果决定
            item->setHidden(false);
            continue;
        }
        item->setHidden(!needle.isEmpty() && !item->text(0).contains(needle, Qt::CaseInsensitive));
    }
    // 自底向上：某组一个子项都没命中就整组隐藏
    for (int i = 0; i < ui->swatchTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = ui->swatchTree->topLevelItem(i);
        bool anyVisible = false;
        for (int j = 0; j < group->childCount(); ++j)
            anyVisible = anyVisible || !group->child(j)->isHidden();
        group->setHidden(!anyVisible);
    }
}

void ColorsPanel::buildSwatchChips()
{
    QTreeWidgetItemIterator it(ui->swatchTree);
    for (; *it; ++it) {
        QTreeWidgetItem *item = *it;
        for (const SwatchEntry &entry : kSwatches) {
            if (item->text(0) != fromUtf8(entry.name))
                continue;
            item->setIcon(0, chipIcon(QColor(entry.rgb)));
            break;
        }
    }
}

void ColorsPanel::buildGradientThumbs()
{
    for (int i = 0; i < ui->gradientList->count(); ++i) {
        QListWidgetItem *item = ui->gradientList->item(i);
        const QIcon icon = gradientThumb(item->text());
        if (icon.isNull())
            continue; // 预设名与占位表对不上：不画图标，走样看得见
        item->setIcon(icon);
        item->setSizeHint(QSize(0, kGradientH + 8));
    }
}

void ColorsPanel::buildPatternThumbs()
{
    for (int i = 0; i < ui->patternList->count(); ++i) {
        QListWidgetItem *item = ui->patternList->item(i);
        item->setIcon(patternThumb(item->text()));
    }
}

void ColorsPanel::syncForegroundSwatch()
{
    const QString color = ui->hexEdit->text();
    // 前景色块用内联样式覆盖（与 .ui 里的初值写法一致）；这里只求视觉自洽，
    // 不代表任何 document / domain 状态。
    const QString sheet = QStringLiteral("background-color: %1; border: 1px solid #202020;").arg(color);
    ui->fgSwatch->setStyleSheet(sheet);
    ui->strip1->setStyleSheet(sheet);
}
