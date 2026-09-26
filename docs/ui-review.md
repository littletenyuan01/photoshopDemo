# UI 结构审查与收口

> 一次针对 `psDemo/ui` 的结构审查记录，以及随之完成的重构。
> 审查方法：通读全部 UI 源码 + 用 grep 实测依赖方向 + 逐条对照 `gimp-master` 的对应模块。
> 结论分「已重构」与「仍欠」两部分；**仍欠部分不得视为已完成**。

---

## 1. 审查范围与方法

| 项目 | 内容 |
|------|------|
| 对象 | `psDemo/` 全部 `.cpp/.h`（约 1400 行）+ 10 个 `.ui` |
| 依赖检查 | `grep` 扫描 `domain/`、`engine/` 是否出现 `ui/`、`QWidget`、`QListWidget` 等 |
| 对照基线 | `gimp-master/app/widgets`、`app/tools`、`app/display`、`app/core` |

### 实测结论（重构前）

| 检查项 | 结果 |
|--------|------|
| `domain/` 依赖 Qt Widgets？ | **否**（0 处命中）—— 铁律守住 |
| `engine/` 依赖 Qt Widgets？ | **否**（0 处命中） |
| UI 直接反写 domain？ | **是，12 处** |
| 工具分发是否硬编码在画布？ | **是**，`CanvasView::mousePressEvent` 内 5 条 if 分支 |
| `documentChanged` 订阅者 | 2 处，其中图层面板据此**全量重建列表** |
| 同一 helper 重复定义 | `applyToolbarIcon` 在 3 个文件逐字重复 |

---

## 2. 已修复问题

### ① 工具分发硬编码在 `CanvasView`（原 P0-1）

**问题**：`CanvasView::mousePressEvent` 里堆着「通用平移 / 抓手 / 缩放放大 / 画笔 / 缩放缩小」
的 if-else 链，且缩放数学写了两遍。每新增一个工具都要改这个文件，工具数与画布体积线性相关。

**修复**：新增 `tools/` 交互层（`architecture.md` §2 早已规划 `tools` + `ToolManager`，此前未实现）。

```text
tools/
├── toolid.h        工具枚举（原有）
├── toolevent.h     ToolEvent：已换算成图像坐标的规范化事件
├── toolcontext.h   ToolContext（文档/颜色/笔刷）+ ViewPort 接口
├── tool.h/.cpp     Tool 基类：mousePress/Move/Release/cursorShape/drawOverlay
├── toolmanager.h/.cpp  注册表 + 活动工具 + 事件转发 + 信号转发
├── movetool.h/.cpp     占位（中性兜底，不消费事件）
├── handtool.h/.cpp     平移（含中键/Alt+左键通用手势）
├── zoomtool.h/.cpp     锚点缩放（左右键）
└── painttool.h/.cpp    画笔+橡皮（同一类的两种 mode）
```

**关键设计**：

- **CanvasView 实现 `ViewPort`**，工具通过 `zoomAt` / `panBy` 请求视图操作 →
  「锚点缩放数学」只存在于 `CanvasView::zoomAt` 一处（此前 wheelEvent 与缩放工具各写一份）。
- **事件在分发前已换算成图像坐标** → 工具不需要知道 zoom/offset，也不引用任何 UI 类型。
- **`ToolManager` 负责信号转发**：活动工具会随用户切换而变，画布无法预先 connect；
  由管理器在切工具时 `rewriteConnections()`，向上暴露 `repaintRequested` /
  `cursorChangeRequested` 供画布一次性连接。
- **`HandTool::isPanGesture`** 供画布优先判定通用平移手势，保证任何工具下都能中键/Alt+左键平移。

**收益**：新增工具 = 加一个类 + 在 `ToolManager` 构造函数注册一行，**CanvasView 与 MainWindow 都不用改**。

> 【对照 GIMP】`app/tools/gimptool.c`（基类虚函数 `button_press`/`motion`/`button_release`/
> `cursor_update`）+ `app/tools/tool_manager.c`（`GimpToolManager`）+ `gimpdisplayshell` 只负责画（持一堆 `GimpCanvasItem*`）。
> 本项目保留形状，去掉 `GimpToolControl`、选项对象、undo extents。

**顺带修掉的真 bug**：原 `CanvasView::setCurrentTool` 只重置 `m_painting`，未清 `m_panning`，
切换工具后平移状态会「粘住」。现在由 `Tool::deactivate()` 统一清理（`HandTool`、`PaintTool` 均已实现）。

### ② UI 直接反写 domain，无收口（原 P0-2）

**问题**：UI 里 12 处直接 `layer->setXxx()` / `layer->pixels()` / `doc->addTransparentLayer()`。
这不是「不优雅」，而是**直接阻断 Phase 6 撤销**：push 语义要求「改动前先推快照」，
若改动散落在 12 个 UI 调用点，事后无法保证不遗漏。

**修复（三层收口）**：

1. `Layer` 增加 **owner 回指**（由 `ImageDocument` 在入栈时 `setOwner`），
   `setName/setVisible/setOpacity/setBlendMode` 内部改值后回调
   `ImageDocument::notifyLayerPropertiesChanged` → 自动广播，UI 无需再手动 `notifyXxx()`。
2. `ImageDocument` 增加**语义化 setter**：`setLayerVisible/Opacity/Name/BlendMode(int index, …)`，
   UI 只调这些，不碰 `Layer` 指针。**Phase 6 只需在这 4 个方法里各加一行 `push_*`。**
3. `layertreepanel.cpp` 改为调用语义化 setter。

> 【对照 GIMP】`GimpItemTreeViewClass` 用字符串声明动作名（`new_action` / `raise_action` /
> `delete_action`…共 14 个），UI 只负责「有这么一个按钮」，具体实现与可撤销性全在 actions 层。
> 本项目尚未建独立 actions 层，但**收口点已就位**（见 §3 欠账）。

### ③ `documentChanged` 一信号包打天下，面板全量重建（原 P0-3）

**问题**：`documentChanged` 同时表示「像素变了」与「属性变了」，图层面板把它接到
`refreshFromDocument()`，而后者是 `clear()` + 全量重建 → 每画一笔都可能丢选中项 /
丢编辑态 / 丢滚动位置。代码里的 `blockUiSignals(true)` **正是在给这个症状打补丁**。

**修复**：信号分级（`ImageDocument`）：

| 信号 | 触发场景 | 订阅方 |
|------|----------|--------|
| `pixelsChanged(QRect)` | 工具/滤镜写像素，带脏区 | 画布（将来只重合成 rect） |
| `layerPropertiesChanged(int)` | 显隐 / 不透明度 / 名称 / 混合模式 | 图层面板（只改第 i 行） |
| `structureChanged()` | 增删 / 排序 | 图层面板（**唯一**需要重建列表的） |
| `activeLayerChanged(int)` | 活动层变了 | 画布 / 面板（只更选中态） |
| `contentChanged()` | 以上任意一种 | 粗粒度订阅方（状态栏、标题） |

图层面板改为三条订阅各有分工，新增 `itemForStackIndex()` 按 `Qt::UserRole` 定位行
（行序会变，**不能用行号当身份**）。

> 【对照 GIMP】`GimpContainerTreeView` 走容器 add/remove/reorder/rename 的**增量** notify，
> 从不 `clear()` 重建。本项目做到同级增量。

### ④ 滑条每次 `valueChanged` 都写 domain

**问题**：`onOpacityChanged` 直连 `valueChanged`，拖一次滑条产生几十次状态变更。
到 Phase 6 时撤销栈会被滑条淹没。

**修复**：两段语义 ——

- `valueChanged` → **只更新百分比文字（预览）**；若 `isSliderDown()` 为真则到此为止；
- `sliderReleased` → `commitOpacity()` 写入 domain；
- `isSliderDown()` 为假（键盘方向键 / 点击轨道）→ 直接提交，保证键盘操作也生效；
- `commitOpacity` 内做**等值判断**，拖回原点不产生状态变更。

结果：**一次操作 = 一次状态变更**。

### ⑤ `MainWindow` 手工逐个 `setDocument`（原 P1-1）

**问题**：换文档要连续调用 `canvasWorkspace` / `canvasView` / `layerPanel` / `docStatusBar`
四处 setDocument（外加 `notifyDocumentChanged`），加一个面板就得加一行，漏一行即静默不刷新。

**修复**：新增 `app/appsession.h/.cpp` —— **文档的唯一持有者与广播中心**
（`architecture.md` §2 早已规划 `AppSession`，此前未实现）。

```text
MainWindow ──setDocument()──> AppSession ──documentChanged(doc)──> CanvasWorkspace / DockPanel / 标题
```

- `CanvasWorkspace::setSession()` 内部订阅并转发给画布与底栏状态条；
- `DockPanel::setSession()` 内部转发给三个 tree panel；
- `MainWindow` 只在构造时交付一次，**不再逐个转发**。

> 【对照 GIMP】`GimpContext` 的 `image-changed` 信号广播；新增 dock 不需要改任何已有代码。

### ⑥ 重复与歧义（原 P1-2 / P1-3 / P1-4）

| 项 | 处理 |
|----|------|
| `applyToolbarIcon` 三处逐字重复 | 提为 `ItemTreePanel::applyToolbarIcon` 静态方法，三处调用改为继承调用 |
| `bool m_updatingScrollBars` 手写守卫 | 改为 `QSignalBlocker`（RAII），提前 return 也不会卡住守卫 |
| `syncRulersAndScrollBars` 里 h/v 各写 15 行 | 抽出 `syncScrollBar()` 与 `handleScroll()`，两轴共用 |
| `LayerPanel` vs `LayerTreePanel` 命名歧义 | 前者改名 **`DockPanel`**（它是三 Tab 停靠壳，不含图层逻辑）；`.ui` 文件名保留 `layerpanel.ui` 以免与 Designer 反复来回 |
| 三个面板各抄一遍「Tab 栏右上角 ≡ 按钮」 | 提为 `PanelChrome::addMenuButton`（`ui/panelchrome.h`），三处共用 |
| `applyToolbarIcon` 图标尺寸写死 24px | 加 `logicalSize` 默认参数：列表底栏仍 24px，更矮的颜色/属性面板底栏用 18px |

---

## 3. 仍欠（**不得视为已完成**）

| # | 欠账 | 影响 | 计划 |
|---|------|------|------|
| 1 | **独立 actions/commands 层未建** | 目前收口在 `ImageDocument` 的语义化 setter；动作的可撤销性、菜单勾选态、快捷键尚无统一注册点 | 与 Phase 6 一并做 |
| 2 | **`Compositor` 仍全量重合成** | `rebuildCache()` 每次 `contentChanged` 都算整图；`pixelsChanged(rect)` 已带脏区但未被使用 | Phase 7 |
| 3 | **工具元数据分散三处** | `toolid.h`（枚举）/ `toolbox.cpp`（图标+中文名+快捷键）/ `tooloptionsbar.cpp`（显示名+提示）。新增工具仍要改 3 个文件 | 建议做 `ToolInfo` 注册表（对照 GIMP `GimpToolInfo`） |
| 4 | **`ToolBox` 仍参与路由** | 工具切换经 `MainWindow::onToolChanged` 中转到画布；理想是 `ToolBox` 只发信号、由 session/context 广播 | 建议随 actions 层一并收敛 |
| 5 | **通道 / 路径面板无 domain** | 列表内容是硬编码占位（RGB/红/绿/蓝、工作路径） | 见 `gimp-reference.mdc` 已知债 |
| 6 | **`ItemTreePanel::addToolbarButton` 未被使用** | 三个面板都在 `.ui` 里静态声明按钮，此方法目前是死代码 | 要么用起来，要么删 |
| 7 | **`Tool::statusMessageRequested` 未被消费** | 管理器有转发信号，但无订阅方 | 接入状态栏时用 |
| 8 | **`uic` 会覆盖 `.ui` 里的 `objectName`** | 本仓库的 `.ui` 存在被 Qt Designer 规范化重写的痕迹 | 见 §5 |
| 9 | **颜色/属性面板的内容是 UI 占位** | 色板分组与色值、渐变/图案预设、调整类型、库类别全是写死的（应从 `.gpl` / `.ggr` 等资源文件读）；底栏动作按钮、对齐按钮无功能 | 接功能时按 §3 的资源载入方式做 |
| 10 | **前景/背景色未进 domain** | 目前只在 `ToolBox → MainWindow::onForegroundColorChanged → CanvasView` 内部串一条线；**没有**进 `AppSession`，新颜色面板与它不通、工具选项栏也看不到它 | 需 `AppSession` 级的前景色字段 + 广播 |

---

## 4. 强制约定（重构后）

1. **依赖方向**：`ui → app → tools/domain`；`engine` 被 `tools`/`domain` 调用。
   **`domain` 与 `engine` 不得出现任何 Qt Widgets 头文件**（`QWidget`/`QListWidget`/`QSlider`…）。
2. **UI 不直接改 `Layer`**：一律走 `ImageDocument` 的语义化 setter。
   > 评审口径：`ui/*.cpp` 里出现 `layer->set*` 或 `->pixels()` 即为**待修 bug**。
3. **新增工具**：加一个 `Tool` 子类 + 在 `ToolManager` 注册一行。
   **不得**在 `CanvasView` 里新增工具分支。
4. **新增面板**：在 `DockPanel` 或 `CanvasWorkspace` 的 `setSession` 里多转发一次。
   **不得**在 `MainWindow` 里手工 `setDocument`。
5. **改像素后必须 `markDirty(rect)`**：`Layer::pixels()` 返回可写引用，不写脏区画布不刷新。
6. **滑条类控件一律两段语义**（拖动预览 / 松手提交），见 `.cursor/rules/percent-sliders.mdc`。
7. **面板样式一律写进 `resources/styles/dark.qss`**，`.ui` 里不写死静态外观
   （只有「值随状态变」的内联样式，如前景色块，才留在代码里）。
   选择器**用类名做祖先**（`ColorsPanel QToolButton`），**不要**用提升实例名
   （`QWidget#colorsPanel …`）—— 实测匹配不上；每个 Tab **页容器要显式写底色**
   （全局 `QWidget` 是透明的，不写就露黑底）。详见 `docs/tech-notes.md` 同名条目。
8. **右侧栏高度必须可拖**：三段放在 `QSplitter` 里，默认比例由
   `MainWindow::applyDefaultRightColumnSizes()` 按真实高度分配（构造期算不准）。
   **新增面板**：给它设 `minimumHeight` 并让 `QSplitter` 收录；
   **页内容高度固定时**（一串控件，不是树/列表）必须套 `QScrollArea`，
   否则面板拖矮后控件会被裁掉、够不着。

---

## 5. 接口稳定性备忘

- `ImageDocument::documentChanged()` 已**移除**，由 `contentChanged()` 取代。
  语义等价（任何视觉变化都会发），迁移只需改信号名。
- `ImageDocument::markDirty()` / `notifyLayerVisualChanged()` / `notifyStructureChanged()`
  已移除；改用 `markDirty(rect)` / `markDirty()`，属性变更由 `Layer` setter 自动广播。
- `CanvasView` 不再有 `setDocument` 由 `MainWindow` 直接调用的路径 ——
  由 `CanvasWorkspace::setSession()` 内部转发；直接调用仍可用（工具间解耦未受影响）。
- **`.ui` 的 `objectName` 会被 Qt Designer 规范化重写**：
  `mainwindow.ui` 中 `dockPanel` 的 `class` / `header` 必须与
  `customwidgets` 段一致（`DockPanel` / `ui/dockpanel.h`）。
  若在 Designer 里保存后 `uic` 报 `dockpanel.h: No such file or directory`，
  先检查该段是否被改写。

---

## 6. 自测清单（改 UI 后跑一遍）

- [ ] 新建 / 打开图像后，画布居中、底栏尺寸与缩放%正确
- [ ] 画笔：在活动层拖拽绘制流畅、不断笔、不污染其他层
- [ ] 橡皮：擦除后棋盘格透出
- [ ] **切换工具后再回画布，平移不会「粘住」**（旧 bug 回归点）
- [ ] 抓手 / 中键 / Alt+左键 三种平移都可拖，且小图居中不可拖出
- [ ] 缩放工具左键放大、右键缩小，锚点在鼠标处
- [ ] 滚轮缩放锚点正确
- [ ] 图层面板：**画笔画一笔后，列表选中项 / 正在编辑的名字 / 滚动位置不丢失**
- [ ] 不透明度滑条：拖动中数字跟随；松手后画布才变化；拖回原点无变化
- [ ] 显隐勾选、双击改名、新建、删除、上下移均即时反映到画布
- [ ] 窗口 → 图层 菜单可显隐右侧面板
- [ ] 窗口 → 颜色 / 属性 可分别显隐两块新面板
- [ ] 颜色面板：改 RGB → 十六进制与前景色块跟着变；拖色相滑杆 → 色域与 RGB 跟着变
- [ ] 色板页：默认展开、每个色名左侧有对应颜色的小色块；搜索框能过滤、清空能恢复
- [ ] 渐变/图案页：每行都有缩略图，且**不同预设的缩略图不一样**
- [ ] 属性页：显示真实的「文档 W×H｜活动图层名」；折叠/展开两个分区（箭头跟着变）
- [ ] 右侧三块面板底色一致（#3a3a3a），**没有露黑底的行/页**
- [ ] 右侧栏**两条拖动条可拖**：拖完高度真的变；把颜色面板压到最矮时该页出现滚动条（控件够得着）
- [ ] 启动时右侧栏默认「图层区最长」，不是被上面两块挤成一条
- [ ] 视图缩放四项（适应窗口 / 100% / 放大 / 缩小）正确
- [ ] 画布外松开鼠标后，绘制不会继续（`buttons` 校验回归点）

---

## 7. GIMP 对照速查

| 本项目 | GIMP 对应 | 简化掉的部分 |
|--------|-----------|-------------|
| `app/AppSession` | `app/core/gimpcontext.c`（`image-changed`） | 多文档、显示列表 |
| `tools/ToolManager` | `app/tools/tool_manager.c`（`GimpToolManager`） | 每显示独立工具状态 |
| `tools/Tool` | `app/tools/gimptool.c` | `GimpToolControl`、选项对象、undo extents |
| `tools/ViewPort` | `GimpDisplayShell` 的 scale/scroll 接口 | 旋转/翻转、参考线、网格 |
| `ui/ItemTreePanel` | `app/widgets/gimpitemtreeview.c` | actions 名字绑定、拖放、多选 |
| `ui/DockPanel` | `dialogs-constructors.c` 的三个独立 dockable | 用户自定义 dock 布局 |
| `ui/ColorsPanel` | `gimpcoloreditor.c` / `gimppaletteeditor.c` / `gimpgradienteditor.c` / 资源工厂视图（**四个** dockable） | 色域自绘、调色板/渐变编辑、`GimpData` 载入 |
| `ui/PropertiesPanel` | **无对应**：变换≈`GimpTransformTool` 选项 + `GimpItem` 尺寸；折叠分区≈`gimp_prop_expanding_frame_new`；调整≈各 GEGL operation；库≈`GimpDataFactoryView` | PS 式的上下文参数、调整/库的实际资源 |
| `ui/PanelChrome` | `gimp_dockbook.c` 的 dock 菜单 | 每个 dock 独立菜单、可拖拽 |
| `ui/CanvasView` | `app/display/gimpdisplayshell.c` | `GimpCanvasItem` 图元体系、旋转 |
| `Layer` owner 回指 | `gimplayer.c` 的 notify 虚函数 | GObject 属性系统 |
