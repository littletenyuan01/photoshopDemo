# 重点代码索引

> 随代码增长持续补充：路径 + 职责 + 为何重要。

## 入口与窗口

| 文件 | 职责 | 状态 |
|------|------|------|
| `psDemo/main.cpp` | `QApplication` 入口 | 已实现 |
| `psDemo/mainwindow.h/.cpp` | 只做菜单接线 + 装配与广播（不再逐个 setDocument） | 已实现 |
| `psDemo/mainwindow.ui` | 主窗口布局；中央 `mainStack`：工作区（选项栏/工具箱/`CanvasWorkspace`/右侧栏）↔ `HomeScreen` | 已实现 |
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
| `tools/tool.h/.cpp` | Tool 基类：`mousePress/Move/Release`、`cursor`/`cursorShape`、`drawOverlay`、`deactivate` |
| `tools/toolcursor.h` | 工具图标光标 / 渐变风格光标辅助 |
| `io/projectio.h/.cpp` | `.pslite` 工程读写（对照 GIMP XCF 的最小子集） |
| `io/psdio.h/.cpp` | `.psd` 子集导出（图层像素；对照 GIMP file-psd 极简） |
| `tools/toolmanager.h/.cpp` | 注册表 + 活动工具 + 事件分发 + **信号转发**（活动工具会变，画布无法预先 connect） |
| `tools/movetool.h/.cpp` | **移动**：点选层 + 平移 offset；变换框浮层（只显示） |
| `tools/transformtool.h/.cpp` | **自由变换**（Ctrl+T）：四角/边/旋转；`FreeTransformOp` 提交 |
| `tools/handtool.h/.cpp` | 平移；`isPanGesture` 供画布判定中键临时平移（空格走临时切工具） |
| `tools/zoomtool.h/.cpp` | 锚点缩放（左键放大 / 右键缩小） |
| `tools/painttool.h/.cpp` | 画笔 + 铅笔 + 橡皮（同一类；铅笔 hardness=1.0；只负责事件→dab，写像素在 PaintEngine） |
| `tools/paintbuckettool.h/.cpp` | 油漆桶 |
| `tools/gradienttool.h/.cpp` | 渐变 |
| `tools/marqueeselecttool.h/.cpp` | **矩形/椭圆选框**：拖拽写入 `Selection`；Shift/Ctrl 加减交 |
| `tools/lassotool.h/.cpp` | **自由套索**：拖拽折线 → `selectPolygon` → `SelectPolygonOp` |
| `tools/polygonallassotool.h/.cpp` | **多边形套索**：单击顶点 / 双击·Enter 闭合；共用 `SelectPolygonOp` |
| `tools/magneticlassotool.h/.cpp` | **磁性套索**：紧固点 + 圆域吸附 + Livewire 段路径；Frequency 自动紧固 → `SelectPolygonOp` |
| `tools/magicwandtool.h/.cpp` | **魔棒**：单击 → `SelectFloodOp`（容差/连续/取样） |
| `tools/quickselecttool.h/.cpp` | **快速选择（精简）**：拖拽连通域扩张 → `SelectFloodOp` |
| `tools/croptool.h/.cpp` | **裁剪**：拖框 + Enter → `ImageDocument::cropTo` |
| `tools/eyedroppertool.h/.cpp` | **吸管**：合成取样 → 前景/背景色 |
| `tools/clonestamptool.h/.cpp` | **仿制图章**：Alt 设源 → `PaintEngine::cloneStampDab` |
| `engine/op/clonestampdabop.h/.cpp` | **CloneStampDabOp**：采样图 + 圆形刷盖度写入瓦片 |
| `tools/focustool.h/.cpp` | **模糊/锐化/涂抹**：→ `PaintEngine::focusDab` |
| `engine/op/focusdabop.h/.cpp` | **FocusDabOp**：可分离盒模糊预计算 / 锐化 / 涂抹 |
| `engine/op/brushcover.h` | 圆形刷盖度（dist² 早退；Focus/Tone/Clone 共用） |
| `engine/op/tilepatch.h` | 瓦片↔矩形补丁 memcpy（Focus/Tone/FloodFill 共用） |
| `tools/tonetool.h/.cpp` | **减淡/海绵**：→ `PaintEngine::toneDab` |
| `engine/op/tonedabop.h/.cpp` | **ToneDabOp**：提亮 / 提高饱和度 |
| `tools/shapetool.h/.cpp` | **形状**：拖框 → `PaintEngine::fillShape` |
| `engine/op/shapefillop.h/.cpp` | **ShapeFillOp**：矩形/椭圆/三角/直线栅格化 |
| `engine/op/freetransformop.h/.cpp` | **FreeTransformOp**：四边形 `quadToQuad` 写入瓦片 |
| `engine/magneticedgesnap.h` | 格点 Sobel（**归一化到 0..255**）+ Width **圆**搜索 + Dijkstra Livewire（f_G 幅值项 + **f_D 方向项**；对照 GIMP `find_max_gradient` / `find_optimal_path`）。标定、成因与验证见 [engine/magnetic-lasso.md](engine/magnetic-lasso.md) |
| `engine/op/selectfloodop.*` | 连通域/相似色写入选区 |

**要点**：新增工具 = 加一个类 + 在 `ToolManager` 注册一行，**`CanvasView` 与 `MainWindow` 均无需改动**。
视图变换由画布实现 `ViewPort` 提供，**锚点缩放数学只存在于 `CanvasView::zoomAt` 一处**。

## domain（文档真相）

| 文件 | 职责 |
|------|------|
| `domain/blendmode.h` | 混合模式枚举：**PS 的 27 种**，顺序 = PS 分组顺序 = `.ui` 项顺序；面板用 `itemData`（含分隔线） |
| `domain/layer.h/.cpp` | 单层属性 + `TileBuffer` + `FilterStack`；**持 owner 回指，setter 内部自动广播** |
| `domain/tilebuffer.h/.cpp` | 64×64 瓦片；新建层统一 `Layer(extent)`，透明不分配、fill/画笔才 `ensureTile` |
| `domain/layerstack.h/.cpp` | 图层列表（`std::vector<unique_ptr>`） |
| `domain/selection.h/.cpp` | **文档级选区 mask**（对照 `GimpSelection`）；`ChannelOp` 加/减/替/交 |
| `domain/filternode.h` | 滤镜节点（`OpName` + 开关 + 参数；对照 drawable filter） |
| `domain/filterstack.h/.cpp` | 图层滤镜栈：增/删/开关；`apply` 对临时图求值，不写瓦片 |
| `domain/imagedocument.h/.cpp` | 文档：尺寸、栈、活动层、**选区**、**duplicateLayer**、分级信号 + 语义化 setter + 累计脏区 |

**要点**：`ImageDocument` 的信号**刻意分级**，让订阅方增量更新而不是整表重建 ——
`pixelsChanged(QRect)` / `layerPropertiesChanged(int)` / `structureChanged()` /
`activeLayerChanged(int)` / `contentChanged()`（汇总）。
UI 不得直接改 `Layer`，一律走 `setLayerVisible/Opacity/Name/BlendMode` 语义化 setter
（这是 Phase 6 撤销的收口点）。

## engine

| 文件 | 职责 |
|------|------|
| `engine/blend.h/.cpp` | 混合色 `B(Cb, Cs)` 算法实现（27 种）；调度优先走 `LayerModeOp` |
| `engine/op/` | `OpName` 枚举+名字表；`OpRegistry`/`OpRunner`；`PointOpRegistry`；`layermodecatalog` |
| `engine/op/layermodeop.*` | 图层混合算子 → 调 `Blend::pixel` / dissolve |
| `engine/op/floodfillop.*` | 油漆桶洪泛算子 |
| `engine/op/gradientop.*` | 渐变填充算子（形状用共享 `GradientType`） |
| `engine/op/stampdabop.*` | 圆形 dab（画笔/橡皮）；模式用共享 `PaintMode` |
| `engine/op/solidfillop.*` | 实色/透明填充（清除、Shift+F5） |
| `engine/op/selectpolygonop.*` | 多边形写入选区（套索；`OpPad::Selection`） |
| `engine/op/selectfloodop.*` | 连通域/相似色写入选区（魔棒；`OpPad::Selection`） |
| `engine/op/opname.*` | `OpName` 枚举 + id/title 名字表 |
| `engine/paintselectionclip.h` | 选区裁剪参数（算子与 PaintEngine 共用） |
| `engine/painttypes.h` | `PaintMode` / `GradientType`（UI/tools/ops 共用） |
| `engine/premul.h` | 预乘/解预乘（compositor 与缓冲算子共用） |
| `engine/compositor.h/.cpp` | 图层遍历 + Alpha `composite_union`；`compositeRegion` 脏区就地更新；颜色经注册表取 `LayerModeOp`；启用滤镜层走临时求值 |
| `engine/projection.h/.cpp` | 文档投影缓存（对照 GimpProjection）；64 块有效位图 + 增量 `sync` |
| `engine/paintengine.h/.cpp` | 门面：`OpRunner::run(OpName, …)`；笔画插值仍在此 |
| `engine/filtereval.h/.cpp` | 滤镜节点求值（亮度/对比度等）；只改临时图 |

**要点**：`blend`（算什么颜色）与 `compositor`（怎么按 Alpha 叠）分开，对齐 GIMP；
算子是可命名调度壳，算法可仍在 `blend` / op 实现文件。绘制 dab 与合成分离。

**要点（算子调度）**：
- `OpRunner` / `PointOpRegistry` 按 `OpName` **常驻实例**（对照 GIMP `gimp_layer_mode_get_operation` 的 per-mode op 缓存）；反复调度只重设参数，不再每次 `new`。
- 算子返回**层内脏矩形**，由调用方直接交给 `markDirty`；工具侧不再自行推算脏区。
- 缓冲算子用 `OpContext::roi` + `OpPaintClip::operationWindow()` 把遍历限制在瓦片窗口，避免整层 `materialize()` / `setFromImage()`（后者会把 TileBuffer 的稀疏瓦片设计废掉）。
- **完整调用链、时序与示例**（含 `prepare→process→finish` 契约、实例复用四条硬约束、坐标系换算、与 GIMP 对照）见 [engine/operators.md](engine/operators.md)。

## ui

| 文件 | 职责 |
|------|------|
| `ui/canvasview.h/.cpp` | **只管视图变换 / 绘制 / 事件归一化转发**；实现 `ViewPort` 供工具请求缩放平移 |
| `ui/canvasworkspace.ui/.h/.cpp` | 顶/左标尺 + 画布 + 底栏状态 + 水平/竖直滚动条；订阅 session |
| `ui/canvasdocstatusbar.ui/.h/.cpp` | 缩放% + 文档信息 + 显示菜单（PS 底栏左侧） |
| `ui/rulerwidget.h/.cpp` | 像素标尺自绘（外层由 workspace.ui 排布） |
| `ui/itemtreepanel.h/.cpp` | Item 树面板基类；提供 `applyToolbarIcon` 等共用能力，以及**缩略图生成**（`makeLayerThumbnail` / `makeChannelThumbnail`、`ThumbChannel`） |
| `ui/layertreepanel.ui/.h/.cpp` | 图层树：列表/**缩略图**/显隐/透明度/增删/**复制**/右键菜单（PS 项占位）；**增量更新 + 缩略图防抖 + 滑条两段提交** |
| `ui/channeltreepanel.*` / `ui/pathtreepanel.*` | 通道树（**缩略图由合成图推算**，无 domain）/ 路径树（无 domain，无缩略图） |
| `ui/dockpanel.h/.cpp` | 右侧三 Tab 停靠壳（图层/通道/路径）；**`.ui` 文件仍名为 `layerpanel.ui`**（类为 `DockPanel`）；订阅 session 后转发给三个树 |
| `ui/colorspanel.ui/.h/.cpp` | 颜色/色板/渐变/图案（对齐 PS 四页）；组内色块/方缩略图网格 |
| `ui/hsvcolorwell.h/.cpp` | PS 式色域：重叠 FG/BG + 二维 S/V + 竖直色相 |
| `ui/propertiespanel.ui/.h/.cpp` | 属性/调整/库停靠面板；「属性」页显示**真实**文档尺寸与活动图层名，可折叠分区 |
| `ui/infopanel.ui/.h/.cpp` | **信息**面板（对照 PS Info）：RGB/CMYK、XY、选区 WH、文档占用、工具说明；F8 / 默认隐藏 |
| `ui/pixmaputils.h` | 位图/图标公共工具：DPR 画布、多档图标光栅化、透明棋盘格（原先在 itemtreepanel / colorspanel / canvasview / toolbox 四处各写一份） |
| `ui/toolbox.ui/.h/.cpp` | 左侧工具箱：17 个占位槽 / 35 个工具，按 PS 分组，右键飞出菜单（对齐 GIMP Toolbox 结构） |
| `ui/tooloptionsbar.ui/.h/.cpp` | 工具选项栏：`QStackedWidget` 11 个工具族参数页，随工具整块切换（对照 GIMP `gimp_tool_options_gui()`）；左端「家」发 `homeClicked`；数值/下拉用 `LabeledLineEdit` / `LabeledComboBox` |
| `ui/labeledlineedit.h/.cpp` | 选项栏「标签+文本框」合体（紧贴、横向不撑开） |
| `ui/labeledcombobox.h/.cpp` | 选项栏「标签+下拉」合体（紧贴、横向不撑开） |
| `ui/homescreen.ui/.h/.cpp` | PS 主页全页壳（新文件/打开/最近项）；对照 GIMP `welcome-dialog.c` Create 页，本项目用栈页 |
| `ui/newdocumentdialog.ui/.h/.cpp` | PS「新建文档」对话框壳；对照 GIMP `image-new-dialog.c` + TemplateEditor |

**要点**：`CanvasView` **不再包含任何工具分支**；工具逻辑全在 `tools/`。

## 图标生成（不在编译产物里，但改图标必看）

| 文件 | 职责 |
|------|------|
| `resources/icons/layers/_gen_svg_icons.py` | 24 个图标（图层 16 / 通道 3 / 路径 5）的 **SVG 矢量**定义与入口 |

**约定**：24×24 viewBox、描边 2.2、round cap/join、主色 `#DCDCDC`、次级色 `#8C8C8C`。
图标是 **SVG 矢量**，由 `ItemTreePanel::svgIcon()` 在显示尺寸上直接光栅化（1x/2x 双分辨率），
任意尺寸、任意 DPI 都锐利 —— 不再用「48px PNG 缩到 22px」的位图方案。
改完跑 `python _gen_svg_icons.py` 重新生成。来源与 iconfont 替换关键词见 `docs/ui/iconfont-icons.md`。

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

## UI 分层约定

- **静态外观**（布局、尺寸、图标、QSS、固定文案）→ 写在对应 `.ui` / `resources/styles/dark.qss`，不要在 `setupUi` 后再 `setStyleSheet` / `setIcon`。
- **动态逻辑**（信号槽、校验器、数据驱动列表、按状态变色、工具槽工厂、高分屏缩略图光栅化）→ 留在 `.cpp`。
- 面板 ≡ 菜单按钮在各面板 `.ui`（`btnPanelMenu`），ctor 里只需 `setCornerWidget`（Designer 无法直接设 Tab 角标）。
- 审查与图标清单：[`docs/ui/`](ui/README.md)；文档/图层/合成概念：[`docs/layers/`](layers/README.md)。
