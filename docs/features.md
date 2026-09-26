# 功能说明

> 只写真实进度。未做功能标「计划中」。

## 已实现

### 主窗口壳

- **说明**：菜单栏按 Photoshop 中文版顶层顺序；其下为工具选项栏；左侧工具箱 + 画布 + 右侧**三段面板**
  （颜色/色板/渐变/图案 → 属性/调整/库 → 图层/通道/路径，对齐 PS 右侧栏的堆叠顺序）。
- **右侧栏三段高度可拖动**（`QSplitter`，两条拖动条）：默认比例 26% / 24% / 50%，
  即**图层区最长**；三段都拖不到折叠（各有 `minimumHeight` 兜底，要隐藏请用「窗口」菜单）。
  颜色页与属性页内容高度固定，面板被拖矮时会**出现纵向滚动条**，控件不会被裁掉够不着。
- **窗口尺寸**：启动时**最大化**（对齐 Photoshop Windows 常见行为）；`.ui` 设计几何约 1440×900，最小 1024×640。Photoshop 本身无固定客户区像素。
- **布局文件**：`mainwindow.ui`、`ui/toolbox.ui`、`ui/tooloptionsbar.ui`、`ui/colorspanel.ui`、`ui/propertiespanel.ui`、`ui/layerpanel.ui`（右侧 `DockPanel` 壳）、`ui/canvasworkspace.ui`
- **已可点**：新建、打开、退出；视图缩放；窗口→图层 / 颜色 / 属性；工具切换；画笔/橡皮绘制活动层；抓手平移；缩放工具；前景/背景色；关于。
- **灰色菜单项**：尚未实现功能占位。
- **如何用**：Qt Creator 打开 `psDemo/psDemo.pro` 运行。选画笔后在画布左键拖拽即可绘制。

### 会话与广播（`app/AppSession`）

- **说明**：`AppSession` 是**当前文档的唯一持有者**，并向所有订阅者广播 `documentChanged(doc)`。
- **为什么重要**：新增面板时只需在 `DockPanel` / `CanvasWorkspace` 的 `setSession` 里多转发一次，
  **`MainWindow` 不需要改动**；早先要手工连续调用四处 `setDocument`，漏一处即静默不刷新。
- **对照 GIMP**：`GimpContext` 的 `image-changed` 信号广播。

### 工具层（`tools/`）

- **说明**：工具是**独立状态机**（`Tool` 子类），由 `ToolManager` 按 id 分发事件；
  画布只把 `QMouseEvent` 归一化成**图像坐标的 `ToolEvent`** 后转发，自身**不含任何工具分支**。
- **已注册**：移动（占位）、抓手、缩放、画笔、橡皮。未接入逻辑的工具回退到「移动」这个**中性兜底**，
  切过去不消费事件，而不是意外继承上一个工具的行为。
- **视图操作**：画布实现 `ViewPort`，工具经 `zoomAt` / `panBy` 请求缩放平移 ——
  锚点缩放数学只存在于 `CanvasView::zoomAt` 一处。
- **收益**：新增工具 = 加一个类 + 在 `ToolManager` 注册一行，**`CanvasView` 与 `MainWindow` 都不用改**。
- **对照 GIMP**：`app/tools/gimptool.c`（虚函数状态机）+ `app/tools/tool_manager.c`（`GimpToolManager`）；
  `gimpdisplayshell` 只负责绘制。本项目去掉 `GimpToolControl`、选项对象、undo extents。

### 标尺与画布居中

- **说明**：工作区上/左有像素标尺（`RulerWidget`），右/下有滚动条；布局在 `canvasworkspace.ui`；文档打开后默认 `zoomFit` 居中。
- **平移约束**：图像小于视口时锁定居中，不能拖出窗口；大于视口时可滚动，边缘钳制，整幅图不会移出可视区。
- **滚动条**：水平/竖直轨道**始终显示**（对齐 PS）；仅当文档超出视口时滑块才有可拖行程。
- **底栏状态**：水平滚动条左侧显示缩放%与文档尺寸（`canvasdocstatusbar.ui`，对齐 PS 截图）；› 可切换像素/厘米显示。
- **对照 GIMP**：`gimp_display_shell_rulers_update` + display scroll；本项目仅像素单位，无参考线。
- **限制**：尚无单位切换（cm/inch）、尚无从标尺拖出参考线。

### 左侧工具箱

- **说明**：**17 个占位槽 / 35 个工具**，分组与顺序对齐 Photoshop 默认工具箱
  （移动 · 选框 · 套索 · 快速选择 · 裁剪 · 吸管 · 画笔 · 图章 · 橡皮擦 · 填充 · 聚焦 · 色调 · 钢笔 · 文字 · 形状 · 抓手 · 缩放）。
  同组共用一个占位，**右键展开子菜单**；工具箱外层是 `QScrollArea`，工具多时可滚动。
- **实现状态（重要）**：只有 **移动 / 抓手 / 缩放 / 画笔 / 橡皮** 有实际逻辑，
  其余 30 个是 **UI 占位**。选中占位工具后 `ToolManager` 回退到中性工具（不消费事件），
  选项栏提示「该工具逻辑尚未接入」。**布局对齐 PS 只为界面完整可演示，不等于功能已实现。**
- **图标**：`resources/icons/tools/`（iconfont 英文命名 PNG），经 `:/icons/tools/` 加载；
  映射表见 `docs/iconfont-icons.md`。

### 工具选项栏（按工具族切换参数）

- **说明**：菜单栏下方一整条，随当前工具**整块切换参数页**，共 **11 个页面**：
  移动 · 选区（含魔棒项）· 裁剪 · 吸管 · 绘画（含图章项）· 油漆桶 · 渐变 · 钢笔/路径 · 文字 · 形状 · 视图。
- **对照 GIMP**：GIMP 用 `GimpToolOptions` 基类 + 每工具族实现 `gimp_tool_options_gui()`，
  由 `gimptooloptions-gui.c` 的 `gimp_prop_*` 按属性生成控件，`gimp-tool-options-manager.c`
  在工具切换时按需创建/替换。本项目取同样形状：**一个 `.ui` 内放 `QStackedWidget`**，
  切换工具即 `setCurrentWidget()`（满足 `qt-ui-forms.mdc`：界面必须落在 `.ui` 里）。
- **参数名对照 GIMP 真实属性**（写在各控件 toolTip 里）：`operation` / `feather-radius` /
  `antialias`（选区）、`paint-mode` / `opacity`（绘画）、`clone-type` / `sample-merged` /
  `align-mode`（图章）、`gradient-type` / `gradient-repeat`（渐变）、`path-polygonal`（路径）。
- **同一页内的专有控件按工具显隐**：选区页的「容差/连续/对所有图层取样」只在魔棒与快速选择出现；
  绘画页的「对齐/对所有图层取样」只在仿制图章出现（对应 GIMP 的 `gimp_clone_options_gui`
  在 paint options 之上追加 clone 项）。
- **⚠️ 实现状态**：**只有「大小」真正生效**（画笔/橡皮直径）。其余所有控件都是 **UI 占位**，
  不改变任何行为；提示语写「该工具逻辑尚未接入（参数为 UI 占位）」。
- **布局**：各页最小宽度不同（实测**绘画页需 1038px**，故选区 609 / 渐变 654 / 文字 700…）。
  整条套一个 `QScrollArea`：**页面保持自然宽度并左对齐**，窗口不够宽时**横向滚动**；
  宽窗口下多余空间留白，**控件不会被拉伸**（若不加对齐，Qt 会把多余空间平均分给各控件，
  在 2000px+ 窗口下控件会被拉得东一个西一个）。
  整条最小宽度仅 **198px**，不会把主窗口撑宽。
- **提示语**：选项条里**只放参数**（对齐 PS）；「参数为 UI 占位，逻辑尚未接入」这类提示
  显示在**状态栏**（`ToolOptionsBar::currentHint()` → `MainWindow::onToolChanged()`）。
- **勾选框样式**：暗色主题下全局 `QWidget` 背景是透明的，原生勾选框指示器会**看不见**，
  故在选项栏样式里显式写了 `QCheckBox::indicator`（描边 + 选中态对勾图），
  与 `colorpickerdialog.ui` 用同一套画法。
- 预览图：`docs/images/tool-options-pages.png`（含宽/窄两种窗口宽度下的表现）。

### 画笔 / 橡皮

- **说明**：在**活动层**像素上绘制或擦除；选项栏可调直径。
- **算法**：`engine/PaintEngine`（圆形 dab + 间距插值）。
- **对照 GIMP**：tools 管事件、paint 写缓冲；本项目对应 `tools/PaintTool` + `engine/PaintEngine`（无 GEGL/笔刷库）。
- **实现要点**：`PaintTool` 一个类承担两个工具（只差 `PaintEngine::Mode`）；
  每次 dab/插值后按「线段包围盒 + 笔刷半径」上报**脏区**（`markDirty(rect)`）。
- **限制**：尚无硬度滑条、流量、选区约束、撤销。

### 新建 / 打开文档

- **新建**：默认 800×600 白底单层「背景」。
- **打开**：常见位图读入为单层文档；合成后显示在画布上。
- **限制**：尚无导出、无工程格式、无多标签文档。

### 图层模型（基础）

- `Layer`：名称、显隐、透明度、混合模式枚举（目前仅 Normal）、`QImage` 像素；
  **持 `owner` 回指**，属性 setter 内部自动广播 `layerPropertiesChanged`。
- `LayerStack`：自底向顶有序层列表。
- `ImageDocument`：尺寸、活动层、**分级信号**（`pixelsChanged(QRect)` / `layerPropertiesChanged(int)` /
  `structureChanged()` / `activeLayerChanged(int)` / 汇总 `contentChanged()`）、
  **累计脏区** `dirtyRect()`，以及供 UI 使用的**语义化 setter**。
- **约定**：UI **不得**直接改 `Layer`，一律走 `ImageDocument::setLayerVisible/Opacity/Name/BlendMode`；
  像素写入后必须 `markDirty(rect)`。
- **限制**：无蒙版/调整层；面板已可操作图层。

### 合成预览

- `Compositor`：自底向顶 Normal + opacity，预乘 Alpha 混合；**支持按矩形脏区合成**。
- `CanvasView`：棋盘格透明底、滚轮缩放、中键/Alt+左键平移、适应窗口 / 100%。
- **限制**：画布目前仍是**全量重合成**（`pixelsChanged` 已带脏区但尚未被消费），见 `wiki/Roadmap.md` Phase 7。

### 颜色 / 色板 / 渐变 / 图案面板（纯 UI 占位）

> 外观见 `docs/images/right-panels.png`（右侧栏从上到下三块面板的实拍）。

- **说明**：`ColorsPanel`（`colorspanel.ui`），右侧栏**最上面一段**，四页 Tab。
  【对照 GIMP】这四样在 GIMP 是**四个独立 dockable**（`app/widgets/gimpcoloreditor.c` /
  `gimppaletteeditor.c` / `gimpgradienteditor.c` / 图案工厂视图），本项目按 PS 外观收进同一停靠区。
- **颜色页**：前景/背景色块 + 色域 + 色相滑杆 + RGB/十六进制。
  ⚠️ 色域是**竖直渐变近似**（样式表做不出 PS 的二维 HSV 方块，真要一致得自绘 QWidget）。
  RGB ↔ 十六进制 ↔ 色相滑杆之间**自洽联动**（改了会互相同步），但**不驱动任何文档状态**
  —— 工程里还没有前景色/背景色的 domain 模型。
- **色板页**（按 PS 截图做细）：搜索框（**真的会按色名过滤**）、顶部最近色块条、色板组树
  （RGB / CMYK / 灰度 / 蜡笔 / 浅色）、底栏三按钮（新建色板组 / 添加当前色 / 删除）。
- **渐变页 / 图案页**：预设列表 + 缩略图；PS 截图未展示这两页内容，故做的是简洁版。
- **缩略图全部由代码现画**（色块 / 渐变条 / 图案格），**不是图片资源**；
  按 2x 设备像素光栅化后打 DPR，HiDPI 下不发糊（同 `ItemTreePanel::svgIcon` 的思路）。
- **限制**：色名与色值、渐变与图案预设都是**写死的占位数据**（真做应从 `.gpl` / `.ggr` 等资源文件读，
  GIMP 走 `GimpData` 载入机制）；底栏三按钮**尚无功能**；
  前景/背景色**没有**进 `AppSession`：工具箱那份颜色只在
  `ToolBox → CanvasView` 内部串（`MainWindow::onForegroundColorChanged`），
  颜色面板与它不通、工具选项栏也看不到它。

### 属性 / 调整 / 库面板（属性页是真实数据）

- **说明**：`PropertiesPanel`（`propertiespanel.ui`），右侧栏**中间一段**，三页 Tab，订阅 `AppSession`。
  ⚠️ **GIMP 没有与这三页一一对应的 dockable**：属性 ≈ `GimpTransformTool` 的选项 + `GimpItem` 的位置尺寸；
  折叠分区 ≈ `app/widgets/gimppropwidgets.c` 的 `gimp_prop_expanding_frame_new`；
  调整 ≈ GIMP「颜色」菜单里的各 GEGL operation；库 ≈ `GimpDataFactoryView` 资源工厂视图。
  故这里按 PS 截图实现，**不硬套 GIMP 结构**（仅在对得上的地方标注出处）。
- **属性页**：顶部一行**真实数据**摘要（`文档 W × H px｜图层 <活动图层名>`，来自 `ImageDocument` / `Layer`）；
  下面「变换」「对齐」两个**可折叠分区**（折叠真的生效：箭头文字与内容体显隐共用同一个开关，
  见 `PropertiesPanel::bindCollapsible`，不会出现「箭头说收起、内容还在」）。
  宽/高 = **活动图层的真实像素尺寸**（只读）；
  ⚠️ **X / Y / 旋转是 UI 占位**：domain 的 `Layer` 既无 offset 也无变换矩阵，tooltip 已注明，**不伪造数值**。
- **调整页**：调整类型列表（亮度/对比度、色阶、曲线…）。
  ⚠️ **尚未接入任何调整算法**，条目的 tooltip 已注明「UI 占位」。
- **库页**：资源类别列表；⚠️ PS 的「库」是云端/团队共享资源面板，本 Demo 无云端资源，仅列类别占位。
- **限制**：属性页**不随内容类型切换**（PS 会按照片/文字/形状换一整套参数），因为工程里还没有这些实体。

### 图层面板

- **说明**：右侧 `DockPanel` 壳（`layerpanel.ui`）内的 `LayerTreePanel`；列表上方为视觉上层。
- **操作**：勾选显隐、双击改名、不透明度滑条、新建、删除。
  ⚠️ **尚无「上移/下移」**：`LayerStack::moveLayer` 已实现但**没有 UI 接线**
  （底栏与「图层」菜单都没有对应按钮/动作），`ImageDocument` 也还没有转发入口
  —— 直接动栈会绕过 `structureChanged` 与 Phase 6 的撤销收口，所以留到接线时一起做。
- **缩略图**：每行左侧显示该层像素的**等比缩略图 + 透明棋盘格衬底**（对齐 PS）；
  全透明层也显示棋盘格，所以「新建的空层」在列表里看得见。生成见 `ItemTreePanel::makeLayerThumbnail`。
- **增量更新**：`structureChanged` 才重建列表；`activeLayerChanged` 只同步选中行；
  `layerPropertiesChanged` 只改受影响那一行 —— **画笔画一笔不会打扰面板的选中项与编辑态**。
- **缩略图防抖**：像素改动不立即重算，而是延迟 ~250ms **只重算活动层那一行**
  （画笔拖动时 `contentChanged` 每帧都发，同步重建会明显拖慢绘制；对齐 PS「停笔后才更新」）。
- **不透明度滑条**：拖动中只做百分比预览，**松手（或键盘操作）才提交**给 domain，
  且提交前做等值判断 → 一次操作 = 一次状态变更（为撤销铺路）。
- **类型筛选行**：按 PS 顺序的 5 个**图标**按钮（像素 / 调整 / 文字 / 形状 / 智能对象）。
  ⚠️ 本项目**目前只有像素层**，其余四类筛不出东西（调整层 Phase 8、智能对象 Phase 5），
  tooltip 已标「尚未实现」，**不做假的筛选逻辑**。
- **锁定行**：PS 四种锁的**图标**（锁定透明像素 / 图像像素 / 位置 / 全部）。
  ⚠️ 当前仅为 UI，尚未接入 domain（点选不会真的限制绘制）。
- **底栏按钮**：加大可点区域（约 30×30）；线框图标见 `resources/icons/layers/`（链接 / fx / 蒙版 / 调整 / 组 / 新建 / 删除），悬停有中文 tip。
- **图标来源**：整套由 `resources/icons/layers/_gen_svg_icons.py` 生成的 **SVG 矢量**，运行时按显示尺寸光栅化（任意尺寸锐利）。**自绘占位**，可按 `docs/iconfont-icons.md` 的关键词从 iconfont.cn 同名替换。
- **限制**：无缩略图尺寸选项；混合模式仍为占位；无撤销（改层后暂不可 Ctrl+Z）；底栏除图层新建/删除外多为 UI 占位。

### 通道面板（缩略图为 UI 推算）

- **说明**：`DockPanel` 内的 `ChannelTreePanel`，列出 RGB 复合通道 + 红/绿/蓝分量 + Alpha。
- **缩略图**：RGB 行为彩色合成缩略图；红/绿/蓝/Alpha 行为**由合成图实时派生的灰度分量图**
  （Alpha 按 PS 习惯反转，白 = 不透明）。生成见 `ItemTreePanel::makeChannelThumbnail`。
- **⚠️ 诚实标注**：**尚无 Channel domain**，这 5 行及其缩略图全部由 `engine/Compositor`
  从合成图**推算**得出，属展示层推算值，**不是真实通道数据**。等通道 domain 开建后应改为读真实通道。
- **刷新策略**（像素变化时）：**250ms 防抖** + **只换图标不重建列表**（否则通道选中项会被打回第 0 行）
  + **面板不可见时完全跳过**（通道 Tab 在后台时只记 dirty，`showEvent` 再补刷）。
  分量缩略图**不做整图中转**（先降到 ≤80×80 再取分量），实测比早期实现快 **3.7×**。
- **限制**：不可增删通道（底栏新建/删除仍为 TODO）、通道不透明度无 domain、无「载入/存储选区」。

### 路径面板

- **说明**：`DockPanel` 内的 `PathTreePanel`，底栏加大 + `:/icons/paths/` 图标已接线。
- **缩略图：刻意不做像素缩略图** —— 路径是**矢量**，没有像素可缩；且 PS 的路径面板本身也不显示缩略图。
  为了让三 Tab 的行高一致，行内给一个**路径标记图标**（`:/icons/paths/new-path.png`），
  **而不是画一条编造的贝塞尔曲线**。真正的轮廓预览要等 path domain 建起来后按路径数据描边。
- **限制**：**尚无 path domain**，仅有「工作路径」占位行；不可增删、不可重命名、无填充/描边/互转选区。

## 计划中（v1 闭环后续）

| 功能 | 简介 | 验收要点 | 状态 |
|------|------|----------|------|
| 画笔 / 橡皮 | 在活动层绘制或擦除 | 不污染其他层 | ✅ 已实现（无撤销） |
| 选区 | 矩形等 + 约束绘制 | 只改选区内 | 计划中 |
| 蒙版 | 灰度蒙版 | 合成正确 | 计划中 |
| 撤销 / 重做 | 绘制与图层操作 | 栈行为正确 | 计划中 |
| 导出 | PNG / JPEG | 导出合成结果 | 计划中 |

## 有余力可选（stretch，非完成标准）

规划见 `wiki/Roadmap.md` Phase 5；**不纳入**当前完善度验收。

| 功能 | 简介 | 状态 |
|------|------|------|
| PSD 导入 / 导出 | 常见分层 PSD 简版读写 | 可选待排期 |
| 智能对象 | 嵌入位图 → 变换 → 栅格化参与合成 | 可选待排期 |
| 插件库 | 极简扩展点（滤镜/导出钩子） | 可选待排期 |
