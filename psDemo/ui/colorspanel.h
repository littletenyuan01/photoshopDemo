#ifndef COLORSPANEL_H
#define COLORSPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ColorsPanel;
}
QT_END_NAMESPACE

/**
 * 颜色 / 色板 / 渐变 / 图案 停靠面板（ui）。
 *
 * 【对照 GIMP】GIMP 里这四样是**四个独立 dockable**：
 *   颜色  → app/widgets/gimpcoloreditor.c（GimpColorEditor，含色域 GimpColorArea + 分量刻度）
 *   色板  → app/widgets/gimppaletteeditor.c（GimpPaletteEditor，资源列表 + 色块网格）
 *   渐变  → app/widgets/gimpgradienteditor.c（GimpGradientEditor，渐变段编辑）
 *   图案  → app/widgets/gimppatternfactoryview.c 一类的资源工厂视图
 * 本项目按 PS 外观把四者收进同一个停靠区的四个 Tab（DockPanel 之于
 * 图层/通道/路径也是同一套处理）。
 *
 * 【当前阶段】只做 UI，不接任何编辑功能：
 * - 色板分组、渐变/图案预设都是**写死的占位数据**，见 .cpp 里的 kSwatches / kGradients
 * - 缩略图（色块 / 渐变条 / 图案格）由代码按目标尺寸光栅化，不是图片资源，
 *   避免「位图缩图发糊」的老问题（同 ItemTreePanel::svgIcon 的处理）
 * - RGB 与十六进制输入框之间是**自洽联动**（改了会互相同步），
 *   但它不驱动任何 domain 状态 —— 工程里目前没有前景色/背景色的 domain 模型
 *
 * 【文档来源】本面板不显示文档数据，故**不订阅** AppSession；
 * 真正需要文档的 PropertiesPanel 才订阅。
 */
class ColorsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorsPanel(QWidget *parent = nullptr);
    ~ColorsPanel() override;

private slots:
    /** RGB 三个微调框任一变化 → 同步十六进制文本与前景色块。 */
    void onRgbChanged();
    /** 用户编辑十六进制文本 → 反解成 RGB（打到一半的非法输入保持原值不动）。 */
    void onHexEdited();
    /** 拖动色相滑杆 → 更新色域方块与 RGB。 */
    void onHueChanged(int hue);
    /** 色板搜索框 → 按色名过滤色板树。 */
    void onSwatchSearchChanged(const QString &text);

private:
    /** 给色板树的每个色名配一块实色缩略图。 */
    void buildSwatchChips();
    /** 给渐变预设列表画渐变条缩略图。 */
    void buildGradientThumbs();
    /** 给图案预设列表画图案缩略图。 */
    void buildPatternThumbs();
    /** 把当前 RGB 写回「前景色」色块（视觉自洽，不代表任何 document 状态）。 */
    void syncForegroundSwatch();

    Ui::ColorsPanel *ui;
};

#endif // COLORSPANEL_H
