# 重点代码索引

> 随代码增长持续补充：路径 + 职责 + 为何重要。

## 入口与窗口

| 文件 | 职责 | 状态 |
|------|------|------|
| `psDemo/main.cpp` | `QApplication` 入口 | 已实现 |
| `psDemo/mainwindow.h/.cpp` | 只做菜单接线 + 装配与广播（不再逐个 setDocument） | 已实现 |
| `psDemo/mainwindow.ui` | 主窗口布局；中央提升为 `CanvasWorkspace`、右侧为 `DockPanel` | 已实现 |
| `psDemo/psDemo.pro` | 源文件与 `INCLUDEPATH`（按 app/domain/engine/tools/ui 分层分组） | 已实现 |

## app（会话与广播）

| 文件 | 职责 |
|------|------|
| `app/appsession.h/.cpp` | **当前文档的唯一持有者与广播中心**；`documentChanged(doc)` 一发，所有面板自行订阅。对照 GIMP `GimpContext` 的 `image-changed` |

**要点**：早先换文档要在 `MainWindow` 里逐个 `setDocument`（画布/工作区/面板/状态条），加一个面板就得加一行，漏一行即静默不刷新。现在只需 `m_session->setDocument(...)`。

## tools（交互状态机）

| 文件 | 职责 |
|------|------|
| `tools/toolid.h` | 工具枚举（对应 GIMP ToolInfo 思路） |
| `tools/toolevent.h` | `ToolEvent`：**已换算成图像坐标**的规范化事件（含控件坐标供锚点缩放用） |
| `tools/toolcontext.h` | `ToolContext`（文档/前景/背景/笔刷半径）+ `ViewPort` 接口 |
| `tools/tool.h/.cpp` | Tool 基类：`mousePress/Move/Release`、`cursorShape`、`drawOverlay`、`deactivate` |
| `tools/toolmanager.h/.cpp` | 注册表 + 活动工具 + 事件分发 + **信号转发**（活动工具会变，画布无法预先 connect） |
| `tools/movetool.h/.cpp` | 占位；**中性兜底**：未接入逻辑的工具切过去不消费事件 |
| `tools/handtool.h/.cpp` | 平移；`isPanGesture` 供画布判定中键 / Alt+左键通用手势 |
| `tools/zoomtool.h/.cpp` | 锚点缩放（左键放大 / 右键缩小） |
| `tools/painttool.h/.cpp` | 画笔 + 橡皮（同一类、两种 mode；只负责事件→dab，写像素在 PaintEngine） |

**要点**：新增工具 = 加一个类 + 在 `ToolManager` 注册一行，**`CanvasView` 与 `MainWindow` 均无需改动**。
视图变换由画布实现 `ViewPort` 提供，**锚点缩放数学只存在于 `CanvasView::zoomAt` 一处**。

## domain（文档真相）

| 文件 | 职责 |
|------|------|
| `domain/blendmode.h` | 混合模式枚举（现仅 Normal） |
| `domain/layer.h/.cpp` | 单层像素与属性；**持 owner 回指，setter 内部自动广播** |
| `domain/layerstack.h/.cpp` | 图层列表（`std::vector<unique_ptr>`） |
| `domain/imagedocument.h/.cpp` | 文档：尺寸、栈、活动层、**分级信号 + 语义化 setter + 累计脏区** |

**要点**：`ImageDocument` 的信号**刻意分级**，让订阅方增量更新而不是整表重建 ——
`pixelsChanged(QRect)` / `layerPropertiesChanged(int)` / `structureChanged()` /
`activeLayerChanged(int)` / `contentChanged()`（汇总）。
UI 不得直接改 `Layer`，一律走 `setLayerVisible/Opacity/Name/BlendMode` 语义化 setter
（这是 Phase 6 撤销的收口点）。

## engine

| 文件 | 职责 |
|------|------|
| `engine/compositor.h/.cpp` | 预乘 Alpha 的 Normal 合成；可按矩形脏区合成 |
| `engine/paintengine.h/.cpp` | 圆形 dab / 线段插值；画笔 SourceOver、橡皮 DestinationOut |

**要点**：`blendNormalPremultiplied` 按扫描线混合；绘制与合成分离（对齐 GIMP paint vs projection）。

## ui

| 文件 | 职责 |
|------|------|
| `ui/canvasview.h/.cpp` | **只管视图变换 / 绘制 / 事件归一化转发**；实现 `ViewPort` 供工具请求缩放平移 |
| `ui/canvasworkspace.ui/.h/.cpp` | 顶/左标尺 + 画布 + 底栏状态 + 水平/竖直滚动条；订阅 session |
| `ui/canvasdocstatusbar.ui/.h/.cpp` | 缩放% + 文档信息 + 显示菜单（PS 底栏左侧） |
| `ui/rulerwidget.h/.cpp` | 像素标尺自绘（外层由 workspace.ui 排布） |
| `ui/itemtreepanel.h/.cpp` | Item 树面板基类；提供 `applyToolbarIcon` 等共用能力，以及**缩略图生成**（`makeLayerThumbnail` / `makeChannelThumbnail`、`ThumbChannel`） |
| `ui/layertreepanel.ui/.h/.cpp` | 图层树：列表/**缩略图**/显隐/透明度/增删排序；**增量更新 + 缩略图防抖 + 滑条两段提交** |
| `ui/channeltreepanel.*` / `ui/pathtreepanel.*` | 通道树（**缩略图由合成图推算**，无 domain）/ 路径树（无 domain，无缩略图） |
| `ui/dockpanel.h/.cpp` | 右侧三 Tab 停靠壳；**`.ui` 文件仍名为 `layerpanel.ui`**（类为 `DockPanel`）；订阅 session 后转发给三个树 |
| `ui/toolbox.ui/.h/.cpp` | 左侧工具箱：17 个占位槽 / 35 个工具，按 PS 分组，右键飞出菜单（对齐 GIMP Toolbox 结构） |
| `ui/tooloptionsbar.ui/.h/.cpp` | 工具选项栏：`QStackedWidget` 11 个工具族参数页，随工具整块切换（对照 GIMP `gimp_tool_options_gui()`） |

**要点**：`CanvasView` **不再包含任何工具分支**；工具逻辑全在 `tools/`。

## 图标生成（不在编译产物里，但改图标必看）

| 文件 | 职责 |
|------|------|
| `resources/icons/layers/_gen_svg_icons.py` | 24 个图标（图层 16 / 通道 3 / 路径 5）的 **SVG 矢量**定义与入口 |

**约定**：24×24 viewBox、描边 2.2、round cap/join、主色 `#DCDCDC`、次级色 `#8C8C8C`。
图标是 **SVG 矢量**，由 `ItemTreePanel::svgIcon()` 在显示尺寸上直接光栅化（1x/2x 双分辨率），
任意尺寸、任意 DPI 都锐利 —— 不再用「48px PNG 缩到 22px」的位图方案。
改完跑 `python _gen_svg_icons.py` 重新生成。来源与 iconfont 替换关键词见 `docs/iconfont-icons.md`。

## 计划中

| 计划类型 | 预期职责 |
|----------|----------|
| `Selection` | 文档级选区 mask |
| `LayerMask` / `AdjustmentLayer` | 蒙版与调整层 |
| `HistoryStack` / `app/commands` | 撤销 / 重做（收口点已在 domain 语义化 setter） |
| `RasterIO` / `ProjectIO` | 导出与工程文件 |
| `ToolInfo` 注册表 | 统一工具元数据（现分散在 toolid/toolbox/tooloptionsbar 三处） |

## 摘录约定

1. 写清文件路径与符号名  
2. 短说明「为什么重要」  
3. 非显然逻辑可附 5–20 行关键片段  

**代码注释**：关键类与算法须在源码中写中文注释，见 `.cursor/rules/code-comments.mdc`。
