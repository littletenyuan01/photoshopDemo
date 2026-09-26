# 技术点与设计决策

## 已确定

| 决策 | 选择 | 理由 |
|------|------|------|
| UI 框架 | Qt 6 Widgets | 课设/简历常见，现有工程已是 Widgets |
| 构建 | qmake（`.pro`） | 与当前 `psDemo` 一致；引入 OpenCV/CUDA 时在 `.pro` 或后续 CMake 中声明 |
| UI 制作 | **Qt Designer `.ui` + 代码逻辑** | 界面可调；业务在 `.cpp`，布局优先落在 `.ui` |
| 像素缓冲 | `QImage` 为主，必要时与 OpenCV `cv::Mat` 互转 | UI 友好；算法侧可用 OpenCV |
| 加速 | **允许** OpenCV / CUDA / CPU 并行 | 热点加速；无 GPU 时 CPU 回退，保证主链路可跑 |
| 功能范围 | 链路完整优先 | 代码量上限约 7 万行；简历要可讲清 |
| 参考开源 | GIMP 分层思路 | 学 paint/tools/core 分离；不复制 GEGL/PDB |

## 加速策略（约定）

1. **正确性优先**：先保证 CPU 路径结果正确，再对热点并行化或上 GPU  
2. **OpenCV**：滤波、几何变换、色彩转换、I/O 辅助等优先考虑  
3. **CUDA**：大图卷积、大批量像素核等算力密集处；需编译开关与运行时检测  
4. **CPU 并行**：`Qt Concurrent` / 线程池 / OpenMP 等，注意与 UI 线程隔离  
5. **文档**：一旦接入依赖，在 `docs/` 与 `wiki/Build.md` 写清安装与开关  

## 待决策（实现前写结论）

- 画布：`QWidget` 自绘 vs `QGraphicsView`
- 是否需要自有工程文件格式（含图层），或 v1 仅导出扁平图
- OpenCV / CUDA 的具体版本与可选编译宏命名

## 已决策（补充）

### 撤销：采用 GIMP 式「推入式 + 每对象一类」

- 【结论】放弃原先「命令模式 vs 图层快照」的二选一，改为 GIMP 的 **push 语义**：**改动之前**先把旧状态推入栈。
- 【理由】命令模式是「事后记录做了什么」，每加一个功能就要设计一个逆操作，且新增入口容易漏记；push 模式把「改状态」这一动作本身强制经过 push 入口，**改文档状态而不 push 即为 bug**。
- 【依据】`gimp-master/app/core/gimpimage-undo-push.c`（46 KB）与 `gimpimage-undo-push.h` 的 50+ 个 `gimp_image_undo_push_*`；每类对象一个 undo 子类。
- 【本项目裁剪】起步 4 类：`pushDrawablePixels` / `pushLayerProp` / `pushLayerStructure` / `pushDocumentProp`。不做 GObject undo 类层次规模。
- 【排期】见 `wiki/Roadmap.md` Phase 6，**前置于按钮功能批量实现**。

### 投影/合成：保持只读，未来按脏区分块

- 【依据】`gimp-master/app/core/gimpprojection.c`、`app/gegl/gimptilehandlervalidate.c`（603 行）、`app/core/gimpchunkiterator.c`；GIMP 3.x 已把 tile 实现下沉 GEGL，GIMP 侧仅剩 91 行适配壳（`gimptilehandlerprojectable.c`）。
- 【本项目】只做脏矩形 + 分块缓存 + 按需重算；**不做**分块稀疏存储、不做优先级渲染线程。
- 【排期】`wiki/Roadmap.md` Phase 7（v1 后加强）。

### 非破坏编辑：只取「滤镜是节点」的语义，不引入 GEGL

- 【依据】`gimp-master/app/core/gimpdrawablefilter.c`、`gimpfilterstack.c`、`app/gegl/gimpapplicator.c`、`app/operations/layer-modes/`。
- 【本项目】只读滤镜节点栈 + 调整层；`Layer` 允许无可编辑像素。**不引入 GEGL**。
- 【排期】`wiki/Roadmap.md` Phase 8（依赖 Phase 7）。

## 技术点日志

### 2026-09 — 颜色 / 属性面板：三个「不报错就是不生效」的坑

> 需求：照 PS 补出右侧的「颜色/色板/渐变/图案」与「属性/调整/库」两块面板。仍是**只做 UI**。

**先说对照**：GIMP 里这七样**没有**一一对应的 dockable —— 颜色/色板/渐变/图案是四个独立
dockable（`gimpcoloreditor.c` / `gimppaletteeditor.c` / `gimpgradienteditor.c` / 资源工厂视图），
属性≈`GimpTransformTool` 的选项 + `GimpItem` 的位置尺寸，折叠分区≈`gimp_prop_expanding_frame_new`，
调整≈「颜色」菜单里的各 GEGL operation，库≈`GimpDataFactoryView`。
故这里按 **PS 截图**实现，只在真能对上的地方标注 GIMP 出处（诚实标注见 `docs/features.md`）。

**坑 1：QSS 祖先选择器写实例名 / 属性，一条都不命中。**
`dark.qss` 里我把面板样式写成 `QWidget#colorsPanel QToolButton { … }`，
运行时 `objectName` 打印确实是 `colorsPanel`、类名也确实是 `ColorsPanel`，**但整段规则全部失效**：
面板底是露出来的黑、分区标题套着全局 `QToolButton:checked` 的蓝底、色板树没有底色。
实测三种写法（`QWidget#colorsPanel …`、`#colorsPanel …`、`*[psPanel="true"] …`）**都不匹配**，
只有**类名**（`ColorsPanel QToolButton { … }`）命中。
判断依据不是猜的：写了屏幕实拍逐控件取色的小程序（见下），改一次量一次。

**坑 2：全局 `QWidget { background-color: transparent; }` 会露出黑底。**
面板根控件命中规则、给了 `#3a3a3a` 也没用 —— `QTabWidget` 的每个 **Tab 页**是独立 QWidget，
透明背景在深色窗口里显示为**黑**。所以**每个页面容器都要显式写底色**，不能指望父级透上来。

**坑 3：`QLatin1String("中文")` 永远比不中。**
占位数据表用 `const char *`（源码是 UTF-8 字节），比较时写成 `item->text(0) != QLatin1String(entry.name)`，
Qt 按 **Latin-1** 解码这些字节 → 一个名字都匹配不上：
**12 个色块图标、6 个渐变缩略图、6 个图案花纹全部静默消失**（图案还全画成同一个默认方块）。
改用 `QString::fromUtf8()` 后正常。

**验证方式**（`no-latent-bugs.mdc` §3/§6/§9 要求的行为验证）：
一次性程序 `QApplication` + 真实 `MainWindow` + 真实 `dark.qss`，等窗口映射后
① 断言行为：RGB↔十六进制联动、非法输入不清色、搜索过滤、分区折叠与箭头、
属性页真实数据；② **屏幕实拍**（`QScreen::grabWindow`）逐控件取色，断言面板底 `#3a3a3a`、
搜索框 `#2e2e2e`、色板树 `#333`、分区标题不是蓝底；③ 断言**图标真的画出来了**
（`icon().isNull()` 全否、且不同预设画出的图**互不相同**）。
共 **32 条断言全过**，构建 0 error / 0 warning。截图见 `docs/images/right-panels.png`。

> **教训（已写进规则 §9）**：光断言「12 行数据都在」是**假验证** —— 图标一个没画也照样通过。
> 样式/图标/资源这类绑定，必须断言**画出来的东西**。

**附带事故**：用 `Get-Content` + `Select-String` + `Set-Content` 从 `dark.qss` 里删临时探针块，
把 210 行样式表**截成 8 行**且首行乱码（下标算成 −1），只能 `git checkout` 后重做。
非 ASCII 文件只能走编辑工具（规则 §7 已加严）。

**补：三段高度必须可拖（用户反馈「最下面的图层面板被挤扁」）**

原来右侧栏是固定的 `QVBoxLayout`：上面两块按自己的 `sizeHint` 把空间占掉，图层区只剩零头。
改成 **`QSplitter`（竖直）**，并踩到两个坑：

1. **`setSizes()` 在构造期算不对**。构造里 `setSizes({250, 240, 460})`，实测出来是
   **250 / 240 / 245** —— 请求总量超过可用高度时，差额被**最后一段**吃掉了，结果图层面板反而最小。
   解法：在 `MainWindow::resizeEvent()` 里按**真实高度**算比例（26% / 24% / 50%），
   并且**只在用户没拖过之前**算（订阅 `QSplitter::splitterMoved` 置位），
   否则用户拖完一改窗口大小就又被覆盖。
2. **给大 `minimumHeight` 不能替代滚动**。既想拖得动、又想控件够得着，就必须让内容能滚：
   颜色页与属性页是**固定高度**的一串控件（面板压到 120px 时内容要 234px），
   故这两页各套一层 `QScrollArea`（`widgetResizable`，无边框、按需出纵向条）。
   **不要**靠调大 `minimumHeight` 来解决 —— 那等于把「谁能变大」的权力从用户手里收回来，
   正是用户提的那个问题的另一面。

**验证方式**（都是行为验证，不是看代码）：

- 直接给**分隔条**发鼠标事件（`QSplitterHandle` 的真实实现路径就是鼠标事件）：
  按住第一条分隔条往下拖 60px → 断言「颜色变高、属性变矮」（实测 192→253 / 177→120，
  属性到底后停在最小高度 120）。
- 断言默认比例：颜色 192 / 属性 177 / 图层 362，**图层区最大**。
- 把颜色面板压到最矮（120）→ 断言颜色页**出现纵向滚动条**（`scrollArea->verticalScrollBar()->isVisible()`），
  即「控件不会够不着」。

### 2026-09 — 性能修复：通道面板刷新（实测 3.7× 提升）

> 起因：代码复查时用微基准量了一次，发现问题比我预想的严重。

**修复前的路径**（`ChannelTreePanel::refreshFromDocument` + `makeChannelThumbnail`）：
每次 `contentChanged`（画笔拖动时**每个鼠标移动事件**）都会
① `itemList->clear()` 整表重建 → **通道选中项被打回第 0 行**（真 bug）；
② 再做**一次全量合成**（画布已经算过一张）；
③ 每个分量建一张**与文档同尺寸**的灰度图逐像素遍历 —— 4000×3000 时
**每个分量分配 48 MB、跑 1200 万次循环，一次刷新 4 个分量 = 192 MB 抖动 + 4800 万次循环**。

**实测（修复前）**：

| 文档 | 1 次合成 | 通道面板刷新 | 倍数 |
|---|---|---|---|
| 800×600 / 2 层 | 1.63 ms | 8.61 ms | ×5.3 |
| 1920×1080 / 3 层 | 9.99 ms | 33.48 ms | ×3.4 |
| 4000×3000 / 4 层 | 60.06 ms | **216.31 ms** | ×3.6 |

4000×3000 下「画布 60 + 通道 216 = 276 ms/次」→ 约 **3.6 FPS**，基本没法画。

**三处改法**：

1. **`makeChannelThumbnail` 不再做整图中转**：先 `scaledToFit(composite, box * 2)`
   把整图降到 ≤80×80（Qt 的降采样是优化过的 C++），**再从缩小后的图取分量**。
   像素循环从 1200 万次降到 6400 次，整图分配归零。
2. **防抖 250ms**：与 `LayerTreePanel` 同一策略，拖动中连发只算一次。
3. **增量更新**：`contentChanged` 只换各行图标，**不 clear() 重建列表** —— 顺带修掉
   「每次画笔移动通道选中项跳回第 0 行」。
4. **不可见时跳过**：通道面板平时在 `DockPanel` 的「图层」Tab 后面不可见，
   此时不做任何计算，只记 `m_thumbDirty`，等 `showEvent` 补刷。

**实测（修复后）**：

| 文档 | 修复前 | 修复后 | 提升 |
|---|---|---|---|
| 800×600 | 8.61 ms | **2.27 ms** | 3.8× |
| 1920×1080 | 33.48 ms | **9.06 ms** | 3.7× |
| 4000×3000 | 216.31 ms | **58.70 ms** | 3.7× |

修复后「面板刷新」耗时 ≈ 「合成」本身（4000×3000：58.7 vs 62.6 ms）
→ **缩略图部分的开销已基本归零**，剩下的是绕不开的合成；
再叠加「防抖 + 不可见跳过」，实际拖动时的开销远低于上表。

**教训**：当初在 `channeltreepanel.cpp` 写「通道数固定 4 行、代价可接受」是**拍脑袋**的，
微基准一量就被推翻。**涉及每帧路径的代码，判断前先量。**

**仍是待办**：`CanvasView::rebuildCache()` 每次 `contentChanged` 仍做全量合成
（4000×3000 时 60 ms）。`ImageDocument::pixelsChanged(QRect)` 的脏区接口早已就位
但一直没用 —— 属 `wiki/Roadmap.md` Phase 7。

### 2026-09 — 工具选项栏：按工具族切换参数页（对齐 GIMP）

> 需求：PS 每个工具都有自己的参数条，demo 也要有。**只做 UI**。

- 【对照 GIMP】先查了 `gimp-master/app/tools` 的真实做法：
  - `GimpToolOptions` 基类 + 每工具族实现 `gimp_tool_options_gui()` 虚函数
  - `gimptooloptions-gui.c` 提供 `gimp_prop_check_button_new` / `gimp_prop_spin_scale_new` /
    `gimp_prop_enum_combo_box_new` / `gimp_prop_expanding_frame_new` 等**按属性生成控件**的构造器
  - `gimp-tool-options-manager.c` 的 `gimp_tools_get_tool_options_gui()` 在工具切换时按需创建/替换整块 GUI
  - 具体参数名直接抄自源码：选区 `operation`/`antialias`/`feather`/`feather-radius`；
    绘画 `paint-mode`/`opacity`/`hard`/dynamics；图章 `clone-type`/`sample-merged`/`align-mode`；
    渐变 `gradient-type`/`distance-metric`/`gradient-repeat`/`offset`/`dither`；
    路径 `path-edit-mode`/`path-polygonal`/`enable-fill`/`enable-stroke`
- 【本项目落地】取同样的**"按工具族整块切换"**形状，但用 Qt 的方式：
  **一个 `.ui` 内放 `QStackedWidget` + 11 个页面**，切换工具即 `setCurrentWidget()`。
  这样满足 `qt-ui-forms.mdc`（界面必须落在 `.ui`，纯代码堆控件是该规则列出的反例），
  Designer 里能直接看到全部页面。
- 覆盖 **35 个工具 → 11 个页面**；同页内的专有控件按工具显隐
  （选区页的魔棒项、绘画页的图章项）。
- ⚠️ **只有「大小」接线**（画笔/橡皮直径）。其余全是 UI 占位，提示语写明「尚未接入」。

**踩坑（两个，都是实测出来的）**：

1. **不要用 `QSizePolicy::Ignored` 去"压缩"选项页**。本意是窄窗口时压缩而非撑宽窗口，
   实际后果是：stack 的自然宽度被忽略，**末尾那个 Expanding 的 spacer 吃掉全部空间**，
   控件被压扁到只剩一位数字（用户截图：`硬度 3` / `不透明度 1`）。
   正确做法是**让页面保持自然宽度**，外面套 `QScrollArea` 横向滚动。
2. **`QScrollArea` 的 sizeHint 是 font-based 的**（`QAbstractScrollArea::sizeHint` 按字号推算，
   实测约 70px），直接把 30px 高的选项条撑高。解法：给滚动区设
   `sizePolicy = Expanding×Fixed` + `minimumSize.height / maximumSize.height`
   把它压回「内容高度 + 横向滚动条」。

**实测数据**（各选项页最小宽度，用临时程序量出）：绘画 **1038** · 文字 700 · 渐变 654 ·
油漆桶 636 · 选区 609 · 路径 607 · 裁剪 559 · 形状 551 · 视图 457 · 吸管 396 · 移动 252。
加了滚动区后整条 `minimumSizeHint` 只有 **218×28**（不会撑宽主窗口），`sizeHint 596×38`。

**另一个坑：勾选框在暗色主题下看不见**。`dark.qss` 里完全没有 `QCheckBox` 样式，
而全局有 `QWidget { background-color: transparent; }`，原生指示器就被"透明"掉了。
解法：在选项栏样式里显式写 `QCheckBox::indicator`（描边 + 选中态对勾图），
抄 `colorpickerdialog.ui` 里已有的画法保持一致。

**第三个坑：宽窗口下控件被拉得东一个西一个**。`QScrollArea` 的 `widgetResizable`
会让页面拿到整个视口宽度，而 Qt 布局会把多余空间**平均分给所有"可增长"的控件**
（`QComboBox` / `QSpinBox` 默认 sizePolicy 都算可增长）。用户窗口 2878px 时尤其明显。
解法：在 `.ui` 里给 stack 的 `<item>` 加
`alignment="Qt::AlignLeft|Qt::AlignVCenter"` —— 布局按 sizeHint 给宽、左对齐，
多余空间留白。实测超宽窗口下：视口 2256px、内容 656px、**无多余滚动条**。

**提示语挪到状态栏**：原先选项条末尾有个 `hintLabel`（"参数为 UI 占位…"），
被 Expanding 的选项区挤到最右边、离参数很远，而且 PS 的选项条本来就只有参数。
已删除该标签，改由 `ToolOptionsBar::currentHint()` 暴露、`MainWindow::onToolChanged()`
显示在状态栏。

### 2026-09 — 修工具箱图标发虚（第二代 DPR 坑）

> 现象：工具箱图标"糊糊的、带灰晕"，而右侧面板图标是清晰的。

**根因**（`ui/toolbox.cpp`）：

```cpp
QPixmap pm(20, 20);
p.drawPixmap(0, 0, base.pixmap(QSize(20, 20)));  // 先缩到 20×20
return QIcon(pm);                                 // 这个 QIcon 只有一张 20×20、DPR=1 的位图
```

屏幕是缩放显示（150%/300%）时 Qt 需要 30/60 像素的图，QIcon 里没有 →
**只能放大那张 20×20 的低分位图 → 发虚带灰晕**。

**改法**：`toolIcon()` 对每档 DPR 都从**源图直接缩放到设备像素尺寸**
（200×200 源图 → 24/48/72，全是降采样所以锐利），再 `setDevicePixelRatio` 后
`addPixmap` 进同一个 QIcon；小三角标记也随 DPR 放大。
同时把按钮图标从 20px 提到 **24px**（与面板底栏一致）。

对比图：`docs/images/toolbox-icon-fix.png`（成对出现，**左旧右新**）。

> **这是同一类坑的第二次出现**（第一次是 SVG 图标的 `render(painter)` + DPR 双重缩放）。
> 教训：**任何"生成位图塞进 QIcon/QPixmap"的代码都必须考虑 devicePixelRatio**——
> 要么多档 DPR，要么让 Qt 从足够大的源图缩。低分位图 + 屏幕缩放 = 必然发虚。

### 2026-09 — 工具箱补齐到 PS 分组（UI 占位）

- 原先只有 12 个占位槽 / 18 个工具，而 `resources/icons/tools/` 里有 38 个图标：
  **图有、没接线**。用户指出工具箱比 PS 少太多。
- 现扩到 **17 槽 / 35 工具**，分组与顺序对齐 PS 默认工具箱
  （移动/选框/套索/快速选择/裁剪/吸管/画笔/图章/橡皮擦/填充/聚焦/色调/钢笔/文字/形状/抓手/缩放）。
- `resources.qrc` 登记的工具图标从 18 → **35**；`toolid.h` 枚举同步扩到 35 项；
  `tooloptionsbar.cpp::toolDisplayName` 必须**逐个补 case**（该 switch 没有 `default`，漏写会触发 `-Wswitch` 警告）。
- 工具箱外层本就有 `QScrollArea`（`toolsScroll`），按钮变多可滚动，未挤爆布局。
- ⚠️ **只有 5 个工具有逻辑**（移动/抓手/缩放/画笔/橡皮）。其余 30 个是 UI 占位：
  选中后 `ToolManager::setActiveTool` 找不到注册项 → 回退到中性 `MoveTool`（不消费事件），
  选项栏显示「该工具逻辑尚未接入」。**这是刻意设计：界面完整可演示，但不假装有功能。**
- 未接线图标剩 3 个：`logo-icon`（应用图标）、`edit`、`smudge-alt`。
- 【已知债】工具元数据仍分散在 `toolbox.cpp` / `tooloptionsbar.cpp` / `toolid.h` 三处，
  加一个工具要改三个文件；计划收敛成 `ToolInfo` 注册表（见 `docs/code-map.md`「计划中」）。

### 2026-09 — 面板图标换 SVG 矢量（解决"缩小就糊"）

> 起因：自绘 PNG 被两次指出"大小不一致、不够清晰"。根因是**位图缩图**。

**根因**：图标文件是 48×48 PNG，底栏按钮显示 22×22，缩小 2.18 倍后描边只剩 ~1.65px，
再经平滑缩放就发软发糊。这是位图方案的硬伤，逐像素重画、加抗锯齿都只是缓解。

**最终方案**：换成 **SVG 矢量**（`resources/icons/layers/_gen_svg_icons.py` 生成 24 个 `.svg`）：

| 项 | 说明 |
|----|------|
| 格式 | SVG，24×24 viewBox（Feather 风格），描边 2.2，round cap/join |
| 渲染 | `ItemTreePanel::svgIcon()` 用 Qt6Svg 在**显示尺寸**上直接光栅化（1x/2x 双分辨率） |
| 效果 | 任意尺寸、任意 DPI 都锐利；`psDemo.pro` 增加 `QT += svg` |

**踩坑记录（真实发生过，肉眼一眼可见）**：

- **不要「先设 `devicePixelRatio` 再 `render(painter)`」**。`QSvgRenderer::render(QPainter*)`
  会按绘制设备的尺寸缩放，而 painter 又叠加一次 DPR 变换 → 图标被画成 2 倍大，
  屏幕上**只看到左上角一小块**（本轮实际出现的 bug）。
  正确做法：在 **1:1 设备像素** 的 `QImage` 上 `render(&painter, QRectF(0,0,px,px))`
  显式指定目标矩形，渲染完再 `QPixmap::setDevicePixelRatio()`。
- **预览工具必须复刻真实渲染路径**：早先预览用 `render(painter)` 且不设 DPR，
  结果预览正常、真机只露一角，白验一轮。`docs/images/icons-preview.png` 的生成程序
  已改为与 `svgIcon()` 同逻辑。
- **SVG 根元素的 `width`/`height` 应与 `viewBox` 一致**（本套 24×24），
  否则其它按固有尺寸渲染的消费者会画错大小。

**走过的弯路**（都已被本方案取代）：早先的 `_iconkit.py`/`_gen_layer_icons.py` 是
48×48 超采样像素方案，已删除。教训：**图标应该用矢量源，而不是在更低位图里堆细节**。

> 【对照 GIMP】GIMP 用矢量 `GimpViewRenderer` 渲染图标。
> 【来源约定】优先 iconfont.cn；本套为自绘占位，搜索关键词见 `docs/iconfont-icons.md`，
> 可同名替换 `.svg`（`resources.qrc` 无需改）。

### 2026-09 — 图层 / 通道面板缩略图

> 纯 UI 阶段（尚未接撤销）。目标是让面板有 PS 那样的缩略图列。

- **生成位置**：统一放在基类 `ItemTreePanel`，避免三个面板各写一份
  （与 `applyToolbarIcon` 同样的去重理由）。
  - `makeLayerThumbnail(layerPixels)`：等比缩放该层像素 + **透明棋盘格衬底**。
    全透明层也能看出「这里有一层」，所以「新建空层」在列表里可见。
  - `makeChannelThumbnail(composite, kind)`：RGB 行给彩色合成图；
    红/绿/蓝/Alpha 行取该分量做灰度图（Alpha 按 PS 习惯反转，白 = 不透明）。
- **两段式缩放**：直接对整张大图做 `SmoothTransformation` 在缩略图频率下太慢，
  改为「先 `FastTransformation` 粗降到 2 倍附近，再平滑收尾」，视觉等价但快得多。
- **预乘格式坑**：分量灰度图必须写成 `qRgba(level, level, level, 255)`。
  若漏掉 alpha，Qt 会按**预乘**规则解释颜色，灰度会整体偏暗。
- **缩略图防抖**：`contentChanged` 在画笔拖动时每帧都发，同步重建缩略图会明显拖慢绘制。
  故延迟 **250ms**（`QTimer` 单次触发，连发只算一次），到期后**只重算活动层那一行**
  （画笔只动活动层）。对齐 PS「停笔后缩略图才更新」的观感。
- **不重建列表**：刷新缩略图只调 `QListWidgetItem::setIcon`，
  不碰文字/勾选/选中态，否则会打断用户正在进行的改名或选择。
- **通道面板的诚实标注**：**尚无 Channel domain**，那 5 行（RGB/红/绿/蓝/Alpha）
  与其缩略图全部由 `engine/Compositor` 从合成图**推算**，属展示层推算值。
  源码与 `docs/features.md` 均已显式标注，等通道 domain 开建后必须换掉。
- **验证方式**：用最小 Qt 程序（`QGuiApplication` + `QT_QPA_PLATFORM=offscreen`）
  把 8 种缩略图渲染成 PNG 拼图肉眼核对，覆盖「白底层 / 透明层 / 空透明层 / 合成 / 四个分量」。
  验证程序为临时件，未入库。
- **路径面板刻意不做像素缩略图**：路径是矢量、没有像素可缩；PS 的路径面板本身也不显示缩略图。
  为统一三 Tab 行高，只在行内给一个路径标记图标（`:/icons/paths/new-path.png`），
  **不画编造的贝塞尔曲线**。真轮廓预览待 path domain 后按路径数据描边。
- 【对照 GIMP】`gimp_viewable_get_preview` → `GimpViewRenderer`（`app/widgets/`）。
  本项目不做异步渲染器与多档尺寸，直接同步生成 40×40。

### 2026-09 — UI 结构收口（工具层 / 会话广播 / 信号分级）

> 完整审查记录见 `docs/ui-review.md`（含审查方法、依赖实测、仍欠清单、自测清单）。

- **新增 `tools/` 交互层**：`Tool` 基类 + `ToolManager` 注册表 + `Move`/`Hand`/`Zoom`/`Paint` 四个工具。
  `CanvasView::mousePressEvent` 里原先 5 条工具 if 分支全部移除；新增工具只需注册一行。
  【对照 GIMP】`app/tools/gimptool.c`（虚函数状态机）+ `app/tools/tool_manager.c`（`GimpToolManager`）。
- **`CanvasView` 实现 `ViewPort`**：工具通过 `zoomAt`/`panBy` 请求视图操作，
  锚点缩放数学从两处（wheelEvent + 缩放工具）收敛为 `CanvasView::zoomAt` 一处。
- **`ToolEvent` 预先换算图像坐标**：工具不再需要 zoom/offset，也不引用任何 UI 类型。
- **新增 `app/AppSession`**：文档唯一持有者 + 广播中心。`MainWindow` 不再逐个 `setDocument`
  （原先要手工调 4 处，加面板就得加一行）。【对照 GIMP】`GimpContext` 的 `image-changed`。
- **`ImageDocument` 信号分级**：`pixelsChanged(QRect)` / `layerPropertiesChanged(int)` /
  `structureChanged()` / `activeLayerChanged(int)` / `contentChanged()`（汇总）。
  修掉了「画一笔就 `clear()` 重建图层列表」导致选中项/编辑态/滚动位置丢失的问题。
  新增 `markDirty(rect)` 记录**累计脏区**（Phase 7 分块重合成的接口就位）。
- **`Layer` 增加 owner 回指**：属性 setter 内部自动广播，UI 不再手动 `notifyLayerVisualChanged()`。
- **`ImageDocument` 语义化 setter**：`setLayerVisible/Opacity/Name/BlendMode(int, …)`。
  UI 不再直接改 `Layer`；**Phase 6 撤销只需在这 4 个方法里各加一行 `push_*`**。
- **不透明度滑条两段语义**：拖动中只更新文字预览，`sliderReleased`（或键盘操作）才提交；
  提交前做等值判断 → **一次操作 = 一次状态变更**。
- **去重与清理**：`applyToolbarIcon` 从 3 份重复提为 `ItemTreePanel` 静态方法；
  滚动条同步的 `bool` 守卫改 `QSignalBlocker`（RAII）；h/v 两轴同步抽成 `syncScrollBar`/`handleScroll`。
- **`LayerPanel` 改名 `DockPanel`**：它是三 Tab 停靠壳，不含图层逻辑，原名与 `LayerTreePanel`
  只差一个词易混。`.ui` 文件名保留 `layerpanel.ui`。
- **修掉真 bug**：原 `setCurrentTool` 只重置 `m_painting`，未清 `m_panning`，
  切换工具后平移状态会「粘住」；现由 `Tool::deactivate()` 统一清理。

### 2026-09 — 画布底栏状态（缩放 / 文档信息）

- 新增 `CanvasDocStatusBar`（`canvasdocstatusbar.ui`）：缩放编辑框 + 尺寸文案 + › 菜单。
- 放在水平滚动条左侧（对齐用户 PS 截图）；缩放随 `viewChanged` 更新，可回车改缩放。
- 文档信息默认按 72 ppi 换算厘米显示；菜单可切像素。分辨率字段尚未进 domain。

### 2026-09 — 画布滚动条与平移钳制

- `canvasworkspace.ui` 增加水平/竖直 `QScrollBar`（对齐 PS 画布区）。
- `CanvasView::clampOffset`：小图居中锁定；大图边缘钳制，禁止整幅拖出视口。
- 滚动条与 `setScrollOffset` / `scrollX|Y` 双向同步。
- Qt 在 `min==max` 时会禁用滚动条；完整可见时用假行程保持 AlwaysOn 外观。

### 2026-09 — 标尺与画布居中

- 新增 `CanvasWorkspace`（`canvasworkspace.ui`）：左上角块 + 顶/左 `RulerWidget` + `CanvasView`。
- 【对照 GIMP】`gimp_display_shell_rulers_update`：lower/upper = 视口边在图像坐标中的值；本项目用 `-offset/zoom`。
- 文档载入后 `zoomFit` 居中；首次 `resize` 若尺寸未就绪则延迟 fit，避免偏左上。
- `RulerWidget` 为自绘例外（无独立 .ui）；外壳必须用 `.ui`。

### 2026-09 — 画笔 / 橡皮（PaintEngine）

- 【对照 GIMP】`app/tools`（事件）与 `app/paint/GimpPaintCore`（写缓冲）分离；本项目为 `CanvasView` 事件 + `PaintEngine` dab。
- 圆形径向渐变 dab；`strokeSegment` 按直径比例间距插值，避免拖动断笔。
- 画笔 `SourceOver`，橡皮 `DestinationOut`；只改 `activeLayer()->pixels()`。
- 未做：撤销瓦片、选区 mask、流量/硬度 UI、笔刷预设。

### 2026-09 — iconfont 图标与工具分组

- 使用 `resources/icons/tools/*.png`（英文文件名）。
- 工具栏占位支持多工具：左键用当前子工具，右键弹出同组列表（对齐 PS 飞出菜单）；多子工具时图标右下角画小三角。
- 形状组：矩形 / 椭圆 / 三角 / 直线；选框组：矩形选框 / 椭圆选框；填充组：油漆桶 / 渐变。

- 主窗口加载 `:/styles/dark.qss`。
- 图标约定：**优先 [iconfont.cn](https://www.iconfont.cn/)**（见 `icon-sources.mdc` / `docs/iconfont-icons.md`）；禁止 Adobe 官方图标；资源文件名用英文。
- 工具箱路径：`:/icons/tools/*.png`。旧自绘 `tool-*.svg` 已移除。

### 2026-09 — 图层面板底栏加大与图标

- 底栏 `toolbarHost` 高度 28→36，按钮约 30×30，图标 22×22。
- 去掉 L/M/G/X 字母占位，改用 `:/icons/layers/*.png` 线框隐喻（链环 / fx / 蒙版方圆 / 半圆 / 文件夹 / 叠层加号 / 垃圾桶）。
- 悬停 tip 保持中文；接线见 `layertreepanel.cpp`。

### 2026-09 — 通道 / 路径面板底栏对齐图层

- 通道、路径底栏同样加大（36 高 / 30×30 按钮），去掉符号字母占位。
- 通道：`:/icons/channels/`（载入选区 / 存储选区 / 新建）；删除共用 `layers/delete.png`。
- 路径：`:/icons/paths/`（填充 / 描边 / 互转选区 / 新建）；删除同上。
- 接线：`channeltreepanel.cpp`、`pathtreepanel.cpp`。

### 2026-09 — 图层/通道/路径面板（对照 GIMP 结构）

- 【对照 GIMP】`gimpitemtreeview` + `gimplayertreeview` / `gimpchanneltreeview` / `gimppathtreeview`；dock 注册见 `dialogs-constructors.c`（三个独立 list view）。
- 本项目：`ItemTreePanel` 基类 → `Layer/Channel/PathTreePanel`；`LayerPanel` 仅为 PS 式 Tab 壳（GIMP 里由用户把三 dock 叠 notebook）。
- 图层已接文档；通道/路径占位。未做：独立 actions/commands 层、分量编辑器、路径 stroke。
- 列表行序与 `LayerStack` 下标相反（UI 顶 = 栈顶）。

### 2026-09 — 预乘 Alpha 合成（Compositor）

- 图层缓冲统一为 `QImage::Format_ARGB32_Premultiplied`，避免直通/预乘混用导致脏边。
- `blendNormalPremultiplied`：`out = src + dst * (1 - src.a)`（已含 opacity 缩放 src）。
- `LayerStack` 使用 `std::vector<unique_ptr>`，因 Qt6 `QVector` 对不可拷贝类型不友好。

### 待记

- `QImage` ↔ `cv::Mat` 生命周期
- CUDA 上传开销
- 撤销瓦片内存
- 工具元数据收成 `ToolInfo` 注册表（现分散在 `toolid.h` / `toolbox.cpp` / `tooloptionsbar.cpp`）
- `Compositor` 用起 `pixelsChanged(rect)` 的脏区（Phase 7）
