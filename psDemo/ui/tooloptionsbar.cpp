/**
 * tooloptionsbar.cpp — 工具选项栏实现（ui 层）。
 *
 * 数值参数用文本框手输；控件类型由 ui_tooloptionsbar.h（自 .ui）提供，本文件不重复 include 控件头。
 */
#include "tooloptionsbar.h"
#include "ui_tooloptionsbar.h"

#include <QSignalBlocker>

namespace {

/** 从合体控件 / 文本框读整数并钳制；解析失败用 @p fallback。 */
int editInt(const LabeledLineEdit *field, int lo, int hi, int fallback)
{
    bool ok = false;
    const int v = field->text().trimmed().toInt(&ok);
    return ok ? qBound(lo, v, hi) : fallback;
}

/** 解析浮点：剥掉末尾 % / ° / 空白。 */
qreal editReal(const LabeledLineEdit *field, qreal fallback)
{
    QString t = field->text().trimmed();
    if (t.endsWith(QLatin1Char('%')) || t.endsWith(QStringLiteral("°")))
        t.chop(1);
    t = t.trimmed();
    bool ok = false;
    const qreal v = t.toDouble(&ok);
    return ok ? v : fallback;
}

/** 失焦写回合法值并回调（范围约束在此完成，不另挂 QIntValidator）。 */
template<typename F>
void wireIntEdit(LabeledLineEdit *field, int lo, int hi, int fallback, F onCommit)
{
    QObject::connect(field, &LabeledLineEdit::editingFinished, field,
                     [field, lo, hi, fallback, onCommit]() {
                         field->setText(QString::number(editInt(field, lo, hi, fallback)));
                         onCommit();
                     });
}

} // namespace

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ToolOptionsBar)
{
    ui->setupUi(this);
    // QWidget 默认不按样式表画背景；不加这行时根/子级透明会透出窗口纯黑
    setAttribute(Qt::WA_StyledBackground, true);

    // 画笔/橡皮直径
    wireIntEdit(ui->brushSizeEdit, 1, 500, 20, [this]() {
        emit brushDiameterChanged(brushDiameter());
    });

    // 油漆桶
    const auto emitFill = [this]() { emit fillOptionsChanged(); };
    wireIntEdit(ui->fillToleranceEdit, 0, 255, 32, emitFill);
    wireIntEdit(ui->fillOpacityEdit, 0, 100, 100, emitFill);
    connect(ui->fillContiguousCheck, &QCheckBox::toggled, this, emitFill);
    connect(ui->fillTypeCombo, &LabeledComboBox::currentIndexChanged, this, emitFill);

    // 魔棒 / 快速选择
    const auto emitSelFlood = [this]() { emit selectionFloodOptionsChanged(); };
    wireIntEdit(ui->selToleranceEdit, 0, 255, 32, emitSelFlood);
    wireIntEdit(ui->selFeatherEdit, 0, 250, 0, []() {}); // 羽化暂未接线，仅规范化显示
    connect(ui->selContiguousCheck, &QCheckBox::toggled, this, emitSelFlood);
    connect(ui->selSampleMergedCheck, &QCheckBox::toggled, this, emitSelFlood);

    // 磁性套索
    const auto emitMag = [this]() { emit magneticLassoOptionsChanged(); };
    wireIntEdit(ui->magWidthEdit, 1, 256, 10, emitMag);
    wireIntEdit(ui->magContrastEdit, 1, 100, 40, emitMag);
    wireIntEdit(ui->magFreqEdit, 1, 100, 57, emitMag);

    // 渐变
    const auto emitGrad = [this]() { emit gradientOptionsChanged(); };
    connect(ui->gradTypeCombo, &LabeledComboBox::currentIndexChanged, this, emitGrad);
    wireIntEdit(ui->gradOpacityEdit, 0, 100, 100, emitGrad);
    wireIntEdit(ui->gradOffsetEdit, 0, 100, 0, emitGrad);
    connect(ui->gradDitherCheck, &QCheckBox::toggled, this, emitGrad);
    connect(ui->gradReverseCheck, &QCheckBox::toggled, this, emitGrad);

    // 仿制图章
    const auto emitClone = [this]() { emit cloneStampOptionsChanged(); };
    connect(ui->paintAlignCheck, &QCheckBox::toggled, this, emitClone);
    connect(ui->paintSampleMergedCheck, &QCheckBox::toggled, this, emitClone);

    // 绘画页占位数值（硬度/不透明度/流量/间距）——仅规范化，未全部接线
    wireIntEdit(ui->paintHardnessEdit, 0, 100, 85, []() {});
    wireIntEdit(ui->paintOpacityEdit, 0, 100, 100, []() {});
    wireIntEdit(ui->paintFlowEdit, 0, 100, 100, []() {});
    wireIntEdit(ui->paintSpacingEdit, 1, 1000, 25, []() {});

    // 裁剪占位宽高
    wireIntEdit(ui->cropWidthEdit, 0, 99999, 800, []() {});
    wireIntEdit(ui->cropHeightEdit, 0, 99999, 600, []() {});

    // 路径 / 文字占位
    wireIntEdit(ui->pathStrokeWidthEdit, 1, 100, 2, []() {});
    wireIntEdit(ui->textSizeEdit, 1, 999, 24, []() {});
    wireIntEdit(ui->textLineSpacingEdit, -200, 500, 0, []() {});

    // 形状
    ui->shapeFillCombo->setCurrentIndex(1);
    const auto emitShape = [this]() { emit shapeOptionsChanged(); };
    connect(ui->shapeFillCombo, &LabeledComboBox::currentIndexChanged, this, emitShape);
    connect(ui->shapeStrokeCombo, &LabeledComboBox::currentIndexChanged, this, emitShape);
    wireIntEdit(ui->shapeWidthEdit, 1, 200, 2, emitShape);
    wireIntEdit(ui->shapeRadiusEdit, 0, 500, 0, emitShape);
    connect(ui->shapeSmoothCheck, &QCheckBox::toggled, this, emitShape);

    // 自由变换选项页（对照 PS Ctrl+T 选项条）
    ui->ftInterpCombo->setCurrentIndex(2); // 两次立方
    const auto emitFt = [this]() { emitFreeTransformParams(); };
    connect(ui->ftXEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftYEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftWEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftHEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftAngleEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftSkewHEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftSkewVEdit, &LabeledLineEdit::editingFinished, this, emitFt);
    connect(ui->ftLinkAspectButton, &QToolButton::toggled, this, [this](bool on) {
        emit freeTransformLinkAspectChanged(on);
    });
    connect(ui->ftInterpCombo, &LabeledComboBox::currentIndexChanged, this, [this](int idx) {
        emit freeTransformInterpolationChanged(idx);
    });
    connect(ui->ftCommitButton, &QToolButton::clicked, this, &ToolOptionsBar::freeTransformCommitClicked);
    connect(ui->ftCancelButton, &QToolButton::clicked, this, &ToolOptionsBar::freeTransformCancelClicked);

    connect(ui->homeButton, &QToolButton::clicked, this, &ToolOptionsBar::homeClicked);

    // 选项条控件默认 StrongFocus：切工具换页时 Qt 会把焦点塞给新页的按钮/勾选框，
    // 空格被当成「点家/勾选」而不是画布临时抓手。图标钮不接焦点；勾选/下拉仅点击聚焦。
    for (auto *btn : findChildren<QToolButton *>())
        btn->setFocusPolicy(Qt::NoFocus);
    for (auto *box : findChildren<QCheckBox *>())
        box->setFocusPolicy(Qt::ClickFocus);
    // 弹出列表是独立顶层窗，祖先 stylesheet 常不生效；直接挂在各自 QComboBox 上
    const QString comboCss = QStringLiteral(
        "QComboBox { background-color:#3a3a3a; color:#eee; border:1px solid #222;"
        "  padding:1px 4px; min-height:22px; }"
        "QComboBox::drop-down { border:none; width:16px; }"
        "QComboBox QAbstractItemView {"
        "  background-color:#ffffff; color:#222222;"
        "  selection-background-color:#e5f1fb; selection-color:#222222; outline:0; }"
        "QComboBox QAbstractItemView::item { color:#222222; min-height:22px; padding:2px 8px; }"
        "QComboBox QAbstractItemView::item:selected,"
        "QComboBox QAbstractItemView::item:hover {"
        "  background-color:#e5f1fb; color:#222222; }");
    for (auto *combo : findChildren<QComboBox *>()) {
        combo->setFocusPolicy(Qt::ClickFocus);
        combo->setStyleSheet(comboCss);
    }

    setCurrentTool(Ps::ToolId::Move);
}

ToolOptionsBar::~ToolOptionsBar()
{
    delete ui;
}

int ToolOptionsBar::brushDiameter() const
{
    return editInt(ui->brushSizeEdit, 1, 500, 20);
}

void ToolOptionsBar::setBrushDiameter(int diameter)
{
    // 仅规范化显示；wireIntEdit 挂的是 editingFinished，setText 不会误触发提交
    ui->brushSizeEdit->setText(QString::number(qBound(1, diameter, 500)));
}

int ToolOptionsBar::fillTolerance() const
{
    return editInt(ui->fillToleranceEdit, 0, 255, 32);
}

bool ToolOptionsBar::fillContiguous() const
{
    return ui->fillContiguousCheck->isChecked();
}

Ps::FillSource ToolOptionsBar::fillSource() const
{
    const int index = ui->fillTypeCombo->currentIndex();
    if (index == static_cast<int>(Ps::FillSource::Background))
        return Ps::FillSource::Background;
    if (index == static_cast<int>(Ps::FillSource::Pattern))
        return Ps::FillSource::Pattern;
    return Ps::FillSource::Foreground;
}

int ToolOptionsBar::fillOpacityPercent() const
{
    return editInt(ui->fillOpacityEdit, 0, 100, 100);
}

int ToolOptionsBar::selTolerance() const
{
    return editInt(ui->selToleranceEdit, 0, 255, 32);
}

bool ToolOptionsBar::selContiguous() const
{
    return ui->selContiguousCheck->isChecked();
}

bool ToolOptionsBar::selSampleMerged() const
{
    return ui->selSampleMergedCheck->isChecked();
}

int ToolOptionsBar::magneticWidth() const
{
    return editInt(ui->magWidthEdit, 1, 256, 10);
}

int ToolOptionsBar::magneticContrast() const
{
    return editInt(ui->magContrastEdit, 1, 100, 40);
}

int ToolOptionsBar::magneticFrequency() const
{
    return editInt(ui->magFreqEdit, 1, 100, 57);
}

Ps::GradientType ToolOptionsBar::gradientType() const
{
    const int index = ui->gradTypeCombo->currentIndex();
    const int max = static_cast<int>(Ps::GradientType::Diamond);
    if (index < 0 || index > max)
        return Ps::GradientType::Linear;
    return static_cast<Ps::GradientType>(index);
}

int ToolOptionsBar::gradientOpacityPercent() const
{
    return editInt(ui->gradOpacityEdit, 0, 100, 100);
}

int ToolOptionsBar::gradientOffsetPercent() const
{
    return editInt(ui->gradOffsetEdit, 0, 100, 0);
}

bool ToolOptionsBar::gradientReverse() const
{
    return ui->gradReverseCheck->isChecked();
}

bool ToolOptionsBar::gradientDither() const
{
    return ui->gradDitherCheck->isChecked();
}

bool ToolOptionsBar::cloneAlign() const
{
    return ui->paintAlignCheck->isChecked();
}

bool ToolOptionsBar::cloneSampleMerged() const
{
    return ui->paintSampleMergedCheck->isChecked();
}

bool ToolOptionsBar::shapeFill() const
{
    return ui->shapeFillCombo->currentIndex() != 0;
}

bool ToolOptionsBar::shapeStroke() const
{
    return ui->shapeStrokeCombo->currentIndex() != 0;
}

int ToolOptionsBar::shapeStrokeWidth() const
{
    return editInt(ui->shapeWidthEdit, 1, 200, 2);
}

int ToolOptionsBar::shapeCornerRadius() const
{
    return editInt(ui->shapeRadiusEdit, 0, 500, 0);
}

bool ToolOptionsBar::shapeAntialias() const
{
    return ui->shapeSmoothCheck->isChecked();
}

void ToolOptionsBar::setCurrentTool(Ps::ToolId id)
{
    ui->toolNameLabel->setText(toolDisplayName(id));

    if (QWidget *page = pageForTool(id))
        ui->optionsStack->setCurrentWidget(page);

    updateToolSpecificControls(id);
    m_hint = hintForTool(id);
}

QWidget *ToolOptionsBar::pageForTool(Ps::ToolId id) const
{
    switch (id) {
    case Ps::ToolId::Move:
        return ui->pageMove;

    case Ps::ToolId::RectSelect:
    case Ps::ToolId::EllipseSelect:
    case Ps::ToolId::Lasso:
    case Ps::ToolId::PolygonalLasso:
    case Ps::ToolId::MagneticLasso:
    case Ps::ToolId::QuickSelect:
    case Ps::ToolId::MagicWand:
        return ui->pageSelection;

    case Ps::ToolId::Crop:
    case Ps::ToolId::PerspectiveCrop:
        return ui->pageCrop;

    case Ps::ToolId::Eyedropper:
        return ui->pageEyedropper;

    case Ps::ToolId::Brush:
    case Ps::ToolId::Pencil:
    case Ps::ToolId::MixerBrush:
    case Ps::ToolId::Eraser:
    case Ps::ToolId::BackgroundEraser:
    case Ps::ToolId::CloneStamp:
    case Ps::ToolId::Blur:
    case Ps::ToolId::Sharpen:
    case Ps::ToolId::Smudge:
    case Ps::ToolId::Dodge:
    case Ps::ToolId::Sponge:
        return ui->pagePaint;

    case Ps::ToolId::PaintBucket:
        return ui->pageFill;

    case Ps::ToolId::Gradient:
        return ui->pageGradient;

    case Ps::ToolId::Pen:
    case Ps::ToolId::FreeformPen:
    case Ps::ToolId::AddAnchorPoint:
        return ui->pagePath;

    case Ps::ToolId::Type:
    case Ps::ToolId::TypeVertical:
        return ui->pageText;

    case Ps::ToolId::ShapeRect:
    case Ps::ToolId::ShapeEllipse:
    case Ps::ToolId::ShapeTriangle:
    case Ps::ToolId::ShapeLine:
        return ui->pageShape;

    case Ps::ToolId::Hand:
    case Ps::ToolId::Zoom:
        return ui->pageView;

    case Ps::ToolId::FreeTransform:
        return ui->pageFreeTransform;
    }
    return ui->pageMove;
}

void ToolOptionsBar::updateToolSpecificControls(Ps::ToolId id)
{
    const bool wandLike = (id == Ps::ToolId::MagicWand || id == Ps::ToolId::QuickSelect);
    // 容差 = LabeledLineEdit 合体控件
    ui->selToleranceEdit->setVisible(wandLike);
    ui->selContiguousCheck->setVisible(wandLike);
    ui->selSampleMergedCheck->setVisible(wandLike);

    const bool magnetic = (id == Ps::ToolId::MagneticLasso);
    ui->magWidthEdit->setVisible(magnetic);
    ui->magContrastEdit->setVisible(magnetic);
    ui->magFreqEdit->setVisible(magnetic);

    const bool cloneLike = (id == Ps::ToolId::CloneStamp);
    ui->paintAlignCheck->setVisible(cloneLike);
    ui->paintSampleMergedCheck->setVisible(cloneLike);
}

QString ToolOptionsBar::hintForTool(Ps::ToolId id)
{
    switch (id) {
    case Ps::ToolId::Move:
        return QObject::tr("点击选中图层并拖拽移动；图层面板同步选中");
    case Ps::ToolId::FreeTransform:
        return QObject::tr("拖角点/边缩放，框内平移，框外旋转；右键切换模式；Enter/✓ 确认，Esc/✕ 取消");
    case Ps::ToolId::Brush:
        return QObject::tr("左键绘制（仅「大小」生效）");
    case Ps::ToolId::Pencil:
        return QObject::tr("左键硬边绘制（硬度 100%；仅「大小」生效）");
    case Ps::ToolId::Eraser:
        return QObject::tr("左键擦除（仅「大小」生效）");
    case Ps::ToolId::PaintBucket:
        return QObject::tr("左键填充；透明层单击即可整层填色");
    case Ps::ToolId::Gradient:
        return QObject::tr("拖拽绘制渐变；类型/偏移/反向对照 GIMP Blend");
    case Ps::ToolId::Hand:
        return QObject::tr("拖拽平移画布");
    case Ps::ToolId::Zoom:
        return QObject::tr("左键放大，右键缩小");
    case Ps::ToolId::RectSelect:
        return QObject::tr("拖拽建立矩形选区；拖中 Shift 正方形；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::EllipseSelect:
        return QObject::tr("拖拽建立椭圆选区；拖中 Shift 正圆；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::Lasso:
        return QObject::tr("拖拽手绘套索；松手闭合；按下 Shift 加选 / Ctrl 减选 / 二者相交");
    case Ps::ToolId::PolygonalLasso:
        return QObject::tr("单击加点；Shift 吸附水平/垂直/垂线；双击/Enter 闭合；Backspace 撤点；Esc 取消");
    case Ps::ToolId::MagneticLasso:
        return QObject::tr("单击起点；移动吸边；频率自动紧固；再单击强制锚；双击/Enter 闭合");
    case Ps::ToolId::MagicWand:
        return QObject::tr("单击按颜色建选区；选项栏调容差/连续/取样；Shift 加选 / Ctrl 减选");
    case Ps::ToolId::QuickSelect:
        return QObject::tr("拖拽扩张选区（连通域）；选项栏调容差/取样；Ctrl 减选");
    case Ps::ToolId::Crop:
        return QObject::tr("拖出裁剪框；Enter/双击确认；Esc 取消；Shift 正方形");
    case Ps::ToolId::Eyedropper:
        return QObject::tr("单击取前景色；Alt+单击取背景色（合成取样）");
    case Ps::ToolId::CloneStamp:
        return QObject::tr("Alt+单击设源；拖拽仿制；选项栏调对齐/取样；大小生效");
    case Ps::ToolId::Blur:
        return QObject::tr("拖拽柔化像素（大小生效）");
    case Ps::ToolId::Sharpen:
        return QObject::tr("拖拽锐化像素（大小生效）");
    case Ps::ToolId::Smudge:
        return QObject::tr("拖拽涂抹像素（大小生效）");
    case Ps::ToolId::Dodge:
        return QObject::tr("拖拽提亮像素（大小生效）");
    case Ps::ToolId::Sponge:
        return QObject::tr("拖拽提高饱和度（大小生效）");
    case Ps::ToolId::ShapeRect:
        return QObject::tr("拖拽画矩形；Shift 正方形；选项栏调填充/描边");
    case Ps::ToolId::ShapeEllipse:
        return QObject::tr("拖拽画椭圆；Shift 正圆；选项栏调填充/描边");
    case Ps::ToolId::ShapeTriangle:
        return QObject::tr("拖拽画三角形；Shift 等比例；选项栏调填充/描边");
    case Ps::ToolId::ShapeLine:
        return QObject::tr("拖拽画直线；Shift 吸附 45°；粗细见选项栏");
    default:
        return QObject::tr("参数为 UI 占位，逻辑尚未接入");
    }
}

QString ToolOptionsBar::toolDisplayName(Ps::ToolId id)
{
    switch (id) {
    case Ps::ToolId::Move: return QObject::tr("移动工具");
    case Ps::ToolId::FreeTransform: return QObject::tr("自由变换");
    case Ps::ToolId::RectSelect: return QObject::tr("矩形选框工具");
    case Ps::ToolId::EllipseSelect: return QObject::tr("椭圆选框工具");
    case Ps::ToolId::Lasso: return QObject::tr("套索工具");
    case Ps::ToolId::PolygonalLasso: return QObject::tr("多边形套索工具");
    case Ps::ToolId::MagneticLasso: return QObject::tr("磁性套索工具");
    case Ps::ToolId::QuickSelect: return QObject::tr("快速选择工具");
    case Ps::ToolId::MagicWand: return QObject::tr("魔棒工具");
    case Ps::ToolId::Crop: return QObject::tr("裁剪工具");
    case Ps::ToolId::PerspectiveCrop: return QObject::tr("透视裁剪工具");
    case Ps::ToolId::Eyedropper: return QObject::tr("吸管工具");
    case Ps::ToolId::Brush: return QObject::tr("画笔工具");
    case Ps::ToolId::Pencil: return QObject::tr("铅笔工具");
    case Ps::ToolId::MixerBrush: return QObject::tr("混合器画笔工具");
    case Ps::ToolId::CloneStamp: return QObject::tr("仿制图章工具");
    case Ps::ToolId::Eraser: return QObject::tr("橡皮擦工具");
    case Ps::ToolId::BackgroundEraser: return QObject::tr("背景橡皮擦工具");
    case Ps::ToolId::PaintBucket: return QObject::tr("油漆桶工具");
    case Ps::ToolId::Gradient: return QObject::tr("渐变工具");
    case Ps::ToolId::Blur: return QObject::tr("模糊工具");
    case Ps::ToolId::Sharpen: return QObject::tr("锐化工具");
    case Ps::ToolId::Smudge: return QObject::tr("涂抹工具");
    case Ps::ToolId::Dodge: return QObject::tr("减淡工具");
    case Ps::ToolId::Sponge: return QObject::tr("海绵工具");
    case Ps::ToolId::Pen: return QObject::tr("钢笔工具");
    case Ps::ToolId::FreeformPen: return QObject::tr("自由钢笔工具");
    case Ps::ToolId::AddAnchorPoint: return QObject::tr("添加锚点工具");
    case Ps::ToolId::Type: return QObject::tr("横排文字工具");
    case Ps::ToolId::TypeVertical: return QObject::tr("直排文字工具");
    case Ps::ToolId::ShapeRect: return QObject::tr("矩形工具");
    case Ps::ToolId::ShapeEllipse: return QObject::tr("椭圆工具");
    case Ps::ToolId::ShapeTriangle: return QObject::tr("三角形工具");
    case Ps::ToolId::ShapeLine: return QObject::tr("直线工具");
    case Ps::ToolId::Hand: return QObject::tr("抓手工具");
    case Ps::ToolId::Zoom: return QObject::tr("缩放工具");
    }
    return QObject::tr("工具");
}

void ToolOptionsBar::emitFreeTransformParams()
{
    if (m_blockFtSync)
        return;
    emit freeTransformParamsEdited(
        editReal(ui->ftXEdit, 0.0),
        editReal(ui->ftYEdit, 0.0),
        editReal(ui->ftWEdit, 100.0),
        editReal(ui->ftHEdit, 100.0),
        editReal(ui->ftAngleEdit, 0.0),
        editReal(ui->ftSkewHEdit, 0.0),
        editReal(ui->ftSkewVEdit, 0.0));
}

void ToolOptionsBar::setFreeTransformParams(qreal x, qreal y, qreal wPercent, qreal hPercent,
                                            qreal angleDeg, qreal skewHDeg, qreal skewVDeg,
                                            bool linkAspect, int interpolationIndex)
{
    m_blockFtSync = true;
    const QSignalBlocker b1(ui->ftXEdit);
    const QSignalBlocker b2(ui->ftYEdit);
    const QSignalBlocker b3(ui->ftWEdit);
    const QSignalBlocker b4(ui->ftHEdit);
    const QSignalBlocker b5(ui->ftAngleEdit);
    const QSignalBlocker b6(ui->ftSkewHEdit);
    const QSignalBlocker b7(ui->ftSkewVEdit);
    const QSignalBlocker b8(ui->ftLinkAspectButton);
    const QSignalBlocker b9(ui->ftInterpCombo);

    ui->ftXEdit->setText(QString::number(x, 'f', 2));
    ui->ftYEdit->setText(QString::number(y, 'f', 2));
    ui->ftWEdit->setText(QString::number(wPercent, 'f', 2) + QLatin1Char('%'));
    ui->ftHEdit->setText(QString::number(hPercent, 'f', 2) + QLatin1Char('%'));
    ui->ftAngleEdit->setText(QString::number(angleDeg, 'f', 2) + QStringLiteral("°"));
    ui->ftSkewHEdit->setText(QString::number(skewHDeg, 'f', 2) + QStringLiteral("°"));
    ui->ftSkewVEdit->setText(QString::number(skewVDeg, 'f', 2) + QStringLiteral("°"));
    ui->ftLinkAspectButton->setChecked(linkAspect);
    ui->ftInterpCombo->setCurrentIndex(qBound(0, interpolationIndex, 2));
    m_blockFtSync = false;
}
