# 功能说明

> 只写真实进度。未做功能标「计划中」。

## 已实现

### 主窗口壳

- **说明**：菜单栏按 Photoshop 中文版顶层顺序；其下为工具选项栏（左端「家」图标
  `:/icons/ui/home.png`）；左侧工具箱 + 画布 + 右侧**三段面板**
  （颜色/色板/渐变/图案 → 属性/调整/库 → 图层/通道/路径，对齐 PS 右侧栏的堆叠顺序）。
  标题栏固定文案 **PhotoshopLite**（不拼文档名/尺寸）；窗口图标为圆角黑底灰色 Ps
  `:/icons/ui/app-logo.png`；Windows 产物为 **PSLite.exe**（`TARGET = PSLite`），任务栏图标靠
  `app-logo.ico` 经 `RC_ICONS` 嵌进 exe
  （仅 `setWindowIcon` 往往只改标题栏，任务栏仍显示系统默认窗体图标）。
- **右侧栏三段高度可拖动**（`QSplitter`，两条拖动条）：默认比例 26% / 24% / 50%，
  即**图层区最长**；三段都拖不到折叠（各有 `minimumHeight` 兜底，要隐藏请用「窗口」菜单）。
  颜色页与属性页内容高度固定，面板被拖矮时会**出现纵向滚动条**，控件不会被裁掉够不着。
- **窗口尺寸**：启动时**最大化**（对齐 Photoshop Windows 常见行为）；`.ui` 设计几何约 1440×900，最小 1024×640。Photoshop 本身无固定客户区像素。
- **退出前确认**：所有关闭路径（右上角 ×、文件→退出、Alt+F4、**标题栏 logo 双击**）
  统一汇到 `MainWindow::closeEvent`；有未保存修改时可选「存储并退出 / 不存储退出 / 取消」。
  ⚠️ 标题栏 logo 的**单击弹系统菜单、双击请求关闭**是 Windows/PS 的平台约定，**不做拦截**。
- **存储**：
  - **Ctrl+S / 存储**：始终写 **`.pslite`** 工程（无路径时弹出「存储为」）。
  - **存储为**：可选 `.pslite`（完整工程）或 `.psd`（子集，供 PS 打开）。
  - **`.pslite`**（`io/projectio.*`）：画布尺寸、活动层、图层（名/显隐/不透明度/混合/offset + PNG）、非空选区。对照 GIMP XCF 思路的自研格式。
  - **`.psd`**（`io/psdio.*`）：RGB 8-bit 分层子集——图层名/可见/不透明度/位置/像素 + 合成预览。对照 GIMP `file-psd` 导出的极简版。
  - **PSD 未写**：选区、组、蒙版、调整层、样式、路径、文字。完整再编辑请用 `.pslite`。
  - 打开：`.pslite` + 常见栅格图（PSD 打开未做）。
  - **置入嵌入的对象**（`Ctrl+Shift+P`）：把位图加进**当前文档**为新图层（居中）；
    对照 GIMP `file_open_layers` /「打开为图层」。无文档时回退为「打开」。
  - **置入链接的对象**（`Ctrl+Alt+Shift+P`）：当前文档加**链接层**（记录源路径 + 缓存像素）；
    对照 GIMP「打开为链接图层」/`GimpLinkLayer`。菜单「更新链接」「栅格化→智能对象」已接线。
  - **画布拖放**：有文档→置入为**嵌入**图层；无文档→打开（对照 `gimpdisplayshell-dnd`）。
  - **打开为智能对象**：新建文档 + 链接层（与置入链接的无文档分支相同）。
  - **未做**：文件监视器自动刷新、链接变换矩阵、矢量链接、完整嵌入式 Smart Object。
- **布局文件**：`mainwindow.ui`、`ui/toolbox.ui`、`ui/tooloptionsbar.ui`、`ui/colorspanel.ui`、`ui/propertiespanel.ui`、`ui/layerpanel.ui`（右侧 `DockPanel` 壳）、`ui/canvasworkspace.ui`
- **已可点**：新建、打开（工程/图像）、**置入嵌入 / 置入链接**、存储 / 存储为、导出 PNG/JPEG、退出；
  编辑→撤销/重做/清除/填充；图像→调整→亮度/对比度（非破坏滤镜）；选择→全选/取消/反选；图像→图像大小 / 画布大小；视图缩放；
  窗口→图层 / 颜色 / 属性；工具切换；画笔/橡皮/油漆桶/渐变；抓手；缩放工具；前景/背景色；关于；
  选项条「家」→ 主页全页。
  快捷键：`Ctrl+Z/Y`、`Delete`、`Shift+F5`、`Ctrl+A`、`Ctrl+Shift+P` 置入嵌入、`Ctrl+Alt+Shift+P` 置入链接、空格临时切抓手（工具箱高亮不变）、`X`/`D` 换色、`[`/`]` 笔刷大小；
  滚轮上下平移、`Ctrl`+滚轮左右平移、`Alt`+滚轮缩放（对照 PS）。
- **图像大小 / 画布大小**：
  - `ui/imagesizedialog.ui`：左侧预览 + 宽高/单位/分辨率/重新采样；确认且勾选重新采样时调用 `ImageDocument::scaleImage`。
  - `ui/canvassizedialog.ui`：当前大小 / 新建大小 / 相对 / 定位锚点 / 扩展颜色；确认后调用 `ImageDocument::resizeCanvas`。
  - 快捷键对齐 PS：`Ctrl+Alt+I` / `Ctrl+Alt+C`。
- **菜单下拉**：顶层与 Photoshop 中文版对齐（文件 / 编辑 / 图像 / 图层 / 文字 / 选择 / 滤镜 / 3D / 视图 / 窗口 / 帮助），
  含子菜单（导出、调整、图层样式、滤镜分类、窗口面板列表等）。**除上列已接线项外全部灰显占位**，
  tooltip 写「UI 占位，功能尚未接入」——只补齐入口，不假装有功能。
- **主页 / 新建文档（UI 壳）**：
  - `ui/homescreen.ui`：对齐 PS Home；对照 GIMP `welcome-dialog.c` Create 页（本项目用栈页而非模态欢迎框）。
  - `ui/newdocumentdialog.ui`：对齐 PS「新建文档」；对照 GIMP `image-new-dialog.c` + TemplateEditor。
  - 预设库持久化、颜色模式/分辨率进 domain 均尚未做。
- **如何用**：Qt Creator 打开 `psDemo/psDemo.pro` 运行。选画笔后在画布左键拖拽即可绘制。

### 会话与广播（`app/AppSession`）

- **说明**：`AppSession` 是**当前文档的唯一持有者**，并向所有订阅者广播 `documentChanged(doc)`。
- **为什么重要**：新增面板时只需在 `DockPanel` / `CanvasWorkspace` 的 `setSession` 里多转发一次，
  **`MainWindow` 不需要改动**；早先要手工连续调用四处 `setDocument`，漏一处即静默不刷新。
- **对照 GIMP**：`GimpContext` 的 `image-changed` 信号广播。

### 工具层（`tools/`）

- **说明**：工具是**独立状态机**（`Tool` 子类），由 `ToolManager` 按 id 分发事件；
  画布只把 `QMouseEvent` 归一化成**图像坐标的 `ToolEvent`** 后转发，自身**不含任何工具分支**。
- **已注册**：移动、**自由变换**、抓手、缩放、画笔、**铅笔**、橡皮、油漆桶、渐变、选区工具族、**裁剪、吸管、仿制图章、模糊/锐化/涂抹、减淡/海绵、形状（矩形/椭圆/三角/直线）**。未接入逻辑的工具回退到「移动」这个**中性兜底**，
  切过去不消费事件，而不是意外继承上一个工具的行为。
- **移动（V）**：按下时按像素点选最上层非透明内容并激活该层（图层面板同步）；
  拖拽平移其文档偏移（`Layer::offsetX/Y`），不搬瓦片像素。
  拖中走 live 预览（底图 + 单层叠回，`previewFreeze`），松手再正式投影；
  对照 GIMP `preview_freeze` + live translate，见 [pending-dev.md](pending-dev.md)。
  对照 `gimpmovetool.c` + `gimp_image_pick_layer` → `gimp_item_translate`。
  未做：选区/路径移动、对齐、「仅移动当前层」开关。
- **自由变换（Ctrl+T）**：编辑→自由变换 / 移动组飞出；选项栏与右键模式（含斜切/扭曲/透视）；
  拖角·边缩放、框内平移、框外旋转；Enter 提交、Esc 取消；会话内逐步撤销；翻转。
  预览写回同一图层（z 序不变）；提交走 `FreeTransformOp`（`quadToQuad`）。
  超出层 extent 时 `expandToIncludeLocal` 扩层防裁切。
  对照 GIMP Unified Transform 骨架（四角→确认再写）；UI 偏 PS。
  **已知风险 / 完善方向**（大层拖拽扩层卡顿等）：见 [pending-dev.md](pending-dev.md)。
  未做：变形（Warp）、完整 GIMP 约束/clip 选项、选区与路径变换。
- **视图操作**：画布实现 `ViewPort`，工具经 `zoomAt` / `panBy` 请求缩放平移 ——
  锚点缩放数学只存在于 `CanvasView::zoomAt` 一处。
- **收益**：新增工具 = 加一个类 + 在 `ToolManager` 注册一行，**`CanvasView` 与 `MainWindow` 都不用改**。
- **对照 GIMP**：`app/tools/gimptool.c`（虚函数状态机）+ `app/tools/tool_manager.c`（`GimpToolManager`）；
  `gimpdisplayshell` 只负责绘制。本项目去掉 `GimpToolControl`、选项对象、undo extents。

### 标尺与画布居中

- **说明**：工作区上/左有像素标尺（`RulerWidget`），右/下有滚动条；布局在 `canvasworkspace.ui`；文档打开后默认 `zoomFit` 居中。
- **平移约束**：含 pasteboard **过滚**（对照 PS）：文档小于视口时仍可拖移，滚动条居中且有行程；
  大于视口时亦可略拖过边缘。边距约为半个视口。
- **滚动条**：水平/竖直轨道始终显示（对齐 PS）；有文档时即有可拖行程（不再在「完整可见」时锁死）。
- **底栏状态**：水平滚动条左侧显示缩放%与文档尺寸（`canvasdocstatusbar.ui`，对齐 PS 截图）；› 可切换像素/厘米显示。
- **对照 GIMP**：`gimp_display_shell_rulers_update` + display scroll；本项目仅像素单位，无参考线。
- **限制**：尚无单位切换（cm/inch）、尚无从标尺拖出参考线。

### 左侧工具箱

- **说明**：**17 个占位槽 / 35 个工具**，分组与顺序对齐 Photoshop 默认工具箱
  （移动 · 选框 · 套索 · 快速选择 · 裁剪 · 吸管 · 画笔 · 图章 · 橡皮擦 · 填充 · 聚焦 · 色调 · 钢笔 · 文字 · 形状 · 抓手 · 缩放）。
  同组共用一个占位，**右键展开子菜单**；工具按钮直接放在 `toolsHost` 里（无滚动条，对齐 PS 左栏）。
- **实现状态（重要）**：只有 **移动 / 自由变换 / 抓手 / 缩放 / 画笔 / 铅笔 / 橡皮 / 油漆桶 / 渐变 / 选区工具族 / 裁剪 / 吸管 / 仿制图章 / 模糊·锐化·涂抹 / 减淡·海绵 / 形状** 有实际逻辑，
  其余是 **UI 占位**。选中占位工具后 `ToolManager` 回退到中性工具（不消费事件），
  选项栏提示「该工具逻辑尚未接入」。**布局对齐 PS 只为界面完整可演示，不等于功能已实现。**
- **油漆桶（G）**：左键单击活动层填充；画布光标为油漆桶图标（倾倒口热点）。
  - **对照 GIMP**：`gimpbucketfilltool.c`（事件）→ `gimpdrawable-bucket-fill.c`（apply）+
    `gimppickable-contiguous-region.cc`（by_seed / by_color）；选项见 `gimpbucketfilloptions.c`
   （`fill-mode` / `threshold` / `fill-transparent` / `sample-merged`…）。
  - **已接线**：容差（≈threshold）、连续（PS；GIMP 相似色固定 by_seed）、前景|背景（≈fill-mode）、不透明度。
  - **简化未做**：图案、paint-mode、sample-merged、对角邻接、抗锯齿软边、线稿填充。
  - **选区**：有选区时只填 mask 内（先算连通域再 ∩ Selection；空选区不约束）。
  - 新建透明层上点一下即可整层填色（种子全透明时按 GIMP 只比 alpha）；**若已有选区则只填选区内**。
- **渐变（G）**：左键拖拽起止，松手写入前景→背景渐变；画布光标为十字+渐变角标。
  - **对照 GIMP**：`gimpgradienttool.c`（拖拽）→ `gimpdrawable-gradient.c` →
    `gimpoperationgradient.c`（逐像素 factor）；选项 `gimpgradientoptions.c`
    （`gradient-type` / `offset` 0..100 / `dither`）+ paint 的 `gradient-reverse` / opacity。
  - **形状映射**：线性→LINEAR，径向→RADIAL，角度→CONICAL_ASYMMETRIC，对称→BILINEAR，菱形→SQUARE。
  - **已接线**：类型 / 不透明度 / 偏移 / 仿色 / 反向。
  - **简化未做**：完整 GimpGradient 多色标、shapeburst/螺旋、repeat、超采样、paint-mode、选区、实时 GEGL 预览。
- **自由套索（L）**：左键拖拽手绘折线，松手首尾闭合写入选区。
  - **对照 GIMP**：`gimpfreeselecttool.c` → `gimp_channel_select_polygon`（scan_convert 闭合折线）。
  - **算子路径**：`LassoTool` → `ImageDocument::selectPolygon` → `PaintEngine::selectPolygon` →
    `OpRunner` → `SelectPolygonOp`（`OpPad::Selection`；硬边、无羽化）。
  - **修饰键**：按下时 Shift 加选 / Ctrl 减选 / Shift+Ctrl 相交（与选框一致）。
  - **简化未做**：编辑顶点、羽化/抗锯齿、GIMP 式自由+折线混绘。
- **多边形套索（L）**：单击落点，橡皮筋跟鼠标；闭合后同样走 `SelectPolygonOp`。
  - **对照 GIMP**：`gimppolygonselecttool.c`（折线 widget + `key_press`）。
  - **闭合**：双击 / Enter / 点近起点（≥3 点）；**Esc** 取消；**Backspace/Delete** 撤末点。
  - **Shift 吸附**（对齐 PS）：橡皮筋/落点约束到水平、垂直、45° 对角，以及**相对前一边的平行/垂直**方向。
  - **基建**：`Tool::keyPress` + `ShortcutOverride`（避免 Delete 误触「清除」）+ `ToolEvent::doubleClick`。
- **磁性套索（L）**：半自动边缘跟踪（非 AI）。
  1. **单击立刻落首锚**（最近像素角点；不阻塞合成）；
  2. **移动** → tip 落在**光标最近格点**（仅在 1~2px 内微调到强边，避免橡皮筋跑到光标前方）；
     Width 圆的完整半径用于**单击强制落锚**时的吸附；
  3. **锚点→tip** 在扩展包围盒内做 **Livewire（Dijkstra 最短代价路径）** 贴边，
     代价 = 梯度幅值项（**先归一化到 0..255**，否则强边代价饱和）+ **方向一致性项**
     （沿边≈0 / 横穿≈255）；
  4. **Frequency** = 自上一锚点起**光标累计行进距离**达到间距后自动再落锚（越高越密）；
     按距离而非「tip 更新次数」，避免锚点密度被鼠标速度绑架。也可单击强制落锚 /
     Backspace·Delete 回退；
  5. 双击 / Enter / 点近起点闭合 → `SelectPolygonOp`。
  - 活动中显示搜索圆；锚点与折线只落在像素角点上。
  - **对照 GIMP**：`gimpiscissorstool.c`（智能剪刀）；**对照 PS**：Width/Contrast/Frequency 交互。
  - **算法与标定细节**见 [engine/magnetic-lasso.md](engine/magnetic-lasso.md)：
    代价函数、参数标定表、GIMP 常量对照，以及「贴边不稳 / 横跳」的成因与修正。
  - **简化未做**：全图 lazy 梯度图、f_Z（拉普拉斯零交叉）项、羽化/抗锯齿。
- **魔棒（W）**：单击按颜色建选区。
  - **对照 GIMP**：`gimpfuzzyselecttool.c` → `gimp_pickable_contiguous_region_by_seed` / `by_color`。
  - **算子**：`SelectFloodOp`（`OpPad::Selection`）；选项栏容差 / 连续 / 对所有图层取样已接线。
  - **修饰键**：Shift 加选 / Ctrl 减选 / 二者相交。
- **快速选择（W，精简）**：拖拽中对笔刷路径采样点做连通域洪泛并扩张选区（非 PS 完整边缘模型）。
  - **算子**：同 `SelectFloodOp`；始终连续；首击可 Replace，后续 Add。
- **裁剪（C）**：拖出矩形，框外变暗；**Enter / 双击**确认，**Esc** 取消；**Shift** 正方形。
  - **对照 GIMP**：`gimpcroptool.c` → `gimp_image_crop`；本项目 `ImageDocument::cropTo`（可撤销）。
  - **简化未做**：透视裁剪、选项栏固定比例/宽高、拖动调整手柄。
- **吸管（I）**：单击取合成像素为前景；**Alt+单击**为背景。
  - **对照 GIMP**：`gimpcolorpickertool.c`；本 Demo 固定 sample-merged。
- **仿制图章（S）**：**Alt+单击**设源；拖拽把源处像素刷到目标。
  - **对照 GIMP**：`gimpclonetool.c` / `gimp_clone_options`（align-mode / sample-merged）。
  - **算子**：`CloneStampDabOp`；经 `PaintEngine::cloneStampDab` / `cloneStrokeSegment`。
  - **选项**：对齐（跨笔保留偏移）、对所有图层取样；笔刷「大小」已接线。
  - **手势例外**：图章下 Alt+左键设源（不再用于平移）；平移用空格临时抓手或中键。
- **模糊 / 锐化 / 涂抹**：拖拽在活动层上局部处理；笔刷「大小」生效。
  - **对照 GIMP**：`gimpconvolvetool.c`（blur/sharpen）/ `gimpsmudgetool.c`。
  - **算子**：`FocusDabOp`（`FocusMode`）；经 `PaintEngine::focusDab` / `focusStrokeSegment`。
  - **简化**：强度固定 0.5；模糊为 5×5 盒滤波；涂抹沿笔画方向拖色。
- **减淡 / 海绵**：拖拽提亮或提高饱和度；笔刷「大小」生效。
  - **对照 GIMP**：`gimpdodgeburntool.c` / `gimpspongetool.c`（本 Demo 无加深 Burn）。
  - **算子**：`ToneDabOp`（`ToneMode`）；经 `PaintEngine::toneDab` / `toneStrokeSegment`。
- **形状（U）**：拖拽把矩形 / 椭圆 / 三角形 / 直线栅格化到活动层（非矢量形状层）。
  - **算子**：`ShapeFillOp`；经 `PaintEngine::fillShape`。
  - **已接线**：填充（无 / 前景；图案与渐变回退前景）、描边（无 / 有）、粗细、圆角（仅矩形）、抗锯齿。
  - **Shift**：矩形正方形、椭圆正圆、三角等比例、直线吸附 45°。
- **图标**：`resources/icons/tools/`（iconfont 英文命名 PNG），经 `:/icons/tools/` 加载；
  映射表见 `docs/ui/iconfont-icons.md`。

### 工具选项栏（按工具族切换参数）

- **说明**：菜单栏下方一整条，**左端「家」按钮**（`homeButton`，图标 `resources/icons/ui/home.png`）+ 工具名 + 随当前工具**整块切换参数页**，共 **11 个页面**：
  移动 · 选区（含魔棒项）· 裁剪 · 吸管 · 绘画（含图章项）· 油漆桶 · 渐变 · 钢笔/路径 · 文字 · 形状 · 视图。
- **对照 GIMP**：GIMP 用 `GimpToolOptions` 基类 + 每工具族实现 `gimp_tool_options_gui()`，
  由 `gimptooloptions-gui.c` 的 `gimp_prop_*` 按属性生成控件，`gimp-tool-options-manager.c`
  在工具切换时按需创建/替换。本项目取同样形状：**一个 `.ui` 内放 `QStackedWidget`**，
  切换工具即 `setCurrentWidget()`（满足 `qt-ui-forms.mdc`：界面必须落在 `.ui` 里）。
- **参数名对照 GIMP 真实属性**（写在各控件 toolTip 里）：`operation` / `feather-radius` /
  `antialias`（选区）、`paint-mode` / `opacity`（绘画）、`clone-type` / `sample-merged` /
  `align-mode`（图章）、`gradient-type` / `gradient-repeat`（渐变）、`path-polygonal`（路径）。
- **同一页内的专有控件按工具显隐**：选区页的「容差/连续/对所有图层取样」只在魔棒与快速选择出现；
  选区页的「宽度/对比度/频率」只在磁性套索出现；
  绘画页的「对齐/对所有图层取样」只在仿制图章出现（对应 GIMP 的 `gimp_clone_options_gui`
  在 paint options 之上追加 clone 项）。
- **⚠️ 实现状态**：**「大小」**（画笔/铅笔/橡皮/图章）、**油漆桶页**（容差/连续/填充/不透明度）、
  **渐变页**（类型/不透明度/偏移/仿色/反向）、**选区页魔棒项**、**选区页磁性套索项（宽度/对比度/频率）**、**绘画页图章项（对齐/取样）**、**形状页（填充/描边/粗细/圆角/抗锯齿）**已接线。其余控件多为 **UI 占位**，
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
- **数值/下拉合体控件**：`LabeledLineEdit`（标签+文本框）、`LabeledComboBox`（标签+下拉）。
  横向 `Maximum` + 组内紧贴，避免宽窗口下 HBox 把标签与框中间拉开；组间用固定弹簧，
  页末 Expanding 弹簧吃掉多余空间。数值框描边见 `dark.qss` 的 `ToolOptionsBar QLineEdit`。
- 预览图：`docs/images/tool-options-pages.png`（含宽/窄两种窗口宽度下的表现）。

### 画笔 / 铅笔 / 橡皮

- **说明**：在**活动层**像素上绘制或擦除；选项栏可调直径。
- **算法**：`engine/PaintEngine`（圆形 dab + 间距插值）。
- **对照 GIMP**：tools 管事件、paint 写缓冲；本项目对应 `tools/PaintTool` + `engine/PaintEngine`（无 GEGL/笔刷库）。
- **实现要点**：`PaintTool` 一个类承担画笔 / 铅笔 / 橡皮（差 `Mode` + `hardness`）；
  铅笔固定硬度 1.0（硬边）；每次 dab/插值后按「线段包围盒 + 笔刷半径」上报**脏区**（`markDirty(rect)`）。
- **限制**：尚无硬度滑条、流量。

### 新建 / 打开 / 置入

- **新建**：弹出「新建文档」对话框；确认后按对话框换算得到的**像素宽高**建白底单层「背景」
  （`ImageDocument::createBlank`）。启动时若无用户新建，主窗仍会先放一份默认空白文档便于演示。
  对话框内 PPI / 单位主要用于物理尺寸↔像素；**尚未写入文档字段**。详见 [`layers/document-canvas.md`](layers/document-canvas.md)。
- **打开**（对照 GIMP `file_open_image`）：常见位图读入为**新文档** + 单层「背景」；工程 `.pslite` 整文档加载。
- **置入嵌入的对象**（对照 GIMP `file_open_layers` + `gimp_image_add_layers`）：
  - 入口：文件→置入嵌入的对象 / `Ctrl+Shift+P`；或把文件拖到画布。
  - `ImageDocument::placeImageAsLayer`：文档尺寸不变，新层像素来自位图，`offset` 居中，可撤销（「置入图层」）。
  - 层名默认用文件名（不含扩展名）。
  - 无当前文档时回退为「打开」（与 GIMP open-as-layers 无 image 时先建文档一致）。
- **置入链接的对象**（对照 GIMP `file_open_as_link_layer` / `GimpLinkLayer` 瘦身版）：
  - 入口：文件→置入链接的对象 / `Ctrl+Alt+Shift+P`。
  - `ImageDocument::placeLinkedImageAsLayer`：层存 `linkPath` + 缓存像素；居中；可撤销。
  - **更新链接**（图层菜单）：从源路径重读像素（`updateLinkedLayer`）。
  - **栅格化→智能对象**：清除路径，保留像素后可绘制（`rasterizeLinkedLayer`）。
  - 链接层禁止画笔/填充/变换等像素编辑（须先栅格化）。
  - 图层面板名称后缀「（链接）」/「（破链）」；`.pslite` v5 读写路径；打开工程时源仍在则自动刷新缓存。
- **限制**：无多标签文档；置入不支持 `.pslite` / PSD；无文件监视器、无链接变换矩阵。

### 图层模型（基础）

> 概念与结构图见 [`docs/layers/`](layers/README.md)；信号/链路对照见 [`layers/data-flow.md`](layers/data-flow.md)。

- `Layer`：名称、显隐、透明度、混合模式、像素在 **`TileBuffer`（64×64 懒分配）**；
  **持 `owner` 回指**，属性 setter 内部自动广播 `layerPropertiesChanged`。
- **新建图层统一路径**（对齐 GIMP `layer_new` → `fill` → `add_layer`）：
  `Layer(名,w,h)` 预定格数 → 可选 `fill` → `addLayer`。
  面板「新建」=`addTransparentLayer`（不 fill，0 块瓦片）；新建文档背景=`fill(白)` 占满覆盖格。
  详见 [`layers/tiles-and-memory.md`](layers/tiles-and-memory.md)、[`layers/data-flow.md`](layers/data-flow.md)。
- `LayerStack`：自底向顶有序层列表。
- `ImageDocument`：尺寸、活动层、**分级信号**（`pixelsChanged(QRect)` / `layerPropertiesChanged(int)` /
  `structureChanged()` / `activeLayerChanged(int)` / 汇总 `contentChanged()`）、
  **累计脏区** `dirtyRect()`，以及供 UI 使用的**语义化 setter**。
- **约定**：UI **不得**直接改 `Layer`，一律走 `ImageDocument::setLayerVisible/Opacity/Name/BlendMode`；
  像素写入走 `tiles()`，之后必须 `markDirty(rect)`。
- **限制**：无调整层；新建层时尚无「选填充类型」对话框（固定透明）；面板已可操作图层。
- **图层蒙版**：`LayerMask` 灰度挂在 `Layer`；合成时 `alpha *= mask`；
  面板底栏 / 菜单可添加、删除、停用、应用、链接；可涂画；写入 `.pslite`。

### 合成预览

> 混合公式与预乘说明见 [`layers/compositing.md`](layers/compositing.md)。

- `Compositor`：自底向顶，每层相对下方合成结果混合；**支持按矩形脏区合成**。
  - **混合模式：PS 的 27 种全部实现**（正常/溶解 · 变暗组 · 变亮组 · 对比组 · 反相组 · 分量组）。
    公式逐条对照 GIMP `gimpoperationlayermode-blend.c`（含 `safe_div`、分量组的 HSV/HSL 处理），
    Alpha 合成对照 `gimpoperationlayermode-composite.c` 的 `composite_union`；
    公式表与已知差异见 [`layers/compositing.md`](layers/compositing.md)。
  - 图层面板 `blendModeCombo` 已接线（`.ui` 里 27 项按 PS 顺序列；选层会回读，改模式即重合成）。
  - **限制**：整条管线 8-bit sRGB，与 GIMP 现代模式（按模式选线性/感知空间）数值不完全一致；
    溶解用坐标哈希而非 PRNG（否则全量重合成会闪）。
- `CanvasView`：棋盘格透明底；滚轮上下 / `Ctrl`+滚轮左右 / `Alt`+滚轮缩放；
  空格临时抓手、中键平移；适应窗口 / 100%。
- **限制**：画布目前仍是**全量重合成**（`pixelsChanged` 已带脏区但尚未被消费），见 `wiki/Roadmap.md` Phase 7。

### 颜色 / 色板 / 渐变 / 图案面板（外观对齐 PS）

> 对照用户提供的 Photoshop 四页截图。

- **说明**：`ColorsPanel`（`colorspanel.ui`），右侧栏颜色段，四页 Tab。
  【对照 GIMP】这四样在 GIMP 是**四个独立 dockable**，本项目按 PS 外观收进同一停靠区。
- **颜色页**：`HsvColorWell`——重叠前景/背景方块 + **二维**饱和度/明度色域 + **竖直**色相条
  （已替换原先的水平色相滑杆与一维渐变近似）。下方保留紧凑 RGB/十六进制读数行。
  色域 ↔ RGB **自洽联动**；前景/背景与左侧工具箱、画布绘制色**双向同步**。
- **色板页**：搜索 + 最近色条 + 可折叠组（RGB/CMYK/灰度/蜡笔），组下为**色块网格** + 底栏（组/加/删）。
  网格外观在 `.ui` 的 `chipGridTemplate`；cpp 只填色块数据并按行数调高度。
- **渐变 / 图案页**：搜索 + 分组 + **方缩略图**网格（模板 `presetGridTemplate`）+ 同款底栏。
  图案缩略为代码占位花纹，非摄影素材。
- **限制**：色名/渐变/图案仍为占位数据；底栏按钮无功能。

### 信息面板（对照 PS Info）

- **说明**：`InfoPanel`（`infopanel.ui`），右侧栏可显隐段；**默认隐藏**；**F8** /「窗口 → 信息」切换。
- **四宫格**：光标下合成像素 **RGB** + 估算 **CMYK**；光标 **X/Y**；当前选区 **W/H**（无选区时空）。
- **底栏**：`文档:占用/划痕`（瓦片真实字节 + 满幅合成粗估）；当前工具说明（与选项栏 hint 同源）。
- **取样**：读 `CanvasView` 投影缓存（不解全图），对照 PS 信息面板实时读数。
- **限制**：CMYK 无 ICC；取样模式/单位下拉为 UI 占位；「»」折叠为占位。

### 属性 / 调整 / 库面板（属性页是真实数据）

- **说明**：`PropertiesPanel`（`propertiespanel.ui`），右侧栏属性段，三页 Tab，订阅 `AppSession`。
  ⚠️ **GIMP 没有与这三页一一对应的 dockable**：属性 ≈ `GimpTransformTool` 的选项 + `GimpItem` 的位置尺寸；
  折叠分区 ≈ `app/widgets/gimppropwidgets.c` 的 `gimp_prop_expanding_frame_new`；
  调整 ≈ GIMP「颜色」菜单里的各 GEGL operation；库 ≈ `GimpDataFactoryView` 资源工厂视图。
  故这里按 PS 截图实现，**不硬套 GIMP 结构**（仅在对得上的地方标注出处）。
- **属性页**：顶部一行**真实数据**摘要（`文档 W × H px｜图层 <活动图层名>`，来自 `ImageDocument` / `Layer`）；
  下面「变换」「对齐」两个**可折叠分区**（折叠真的生效：箭头文字与内容体显隐共用同一个开关，
  见 `PropertiesPanel::bindCollapsible`，不会出现「箭头说收起、内容还在」）。
  宽/高 = **活动图层的真实像素尺寸**（只读）；
  X / Y = **活动层 offset**（domain 已有；属性页数值接线可后续补）；旋转仍为 UI 占位。
- **调整页**：调整类型列表（亮度/对比度、色阶、曲线…）。
  ⚠️ **尚未接入任何调整算法**，条目的 tooltip 已注明「UI 占位」。
- **库页**：资源类别列表；⚠️ PS 的「库」是云端/团队共享资源面板，本 Demo 无云端资源，仅列类别占位。
- **限制**：属性页**不随内容类型切换**（PS 会按照片/文字/形状换一整套参数），因为工程里还没有这些实体。

### 图层面板

- **说明**：右侧 `DockPanel` 壳（`layerpanel.ui`）内的 `LayerTreePanel`；列表上方为视觉上层。
- **图层行组件**：每一层 `new LayerRowWidget`（`layerrowwidget.ui`）经 `setItemWidget` 挂到列表；
  布局为眼睛 / 缩略图 / 名称 / `fx` / 展开箭头，下方缩进「效果」组头 + 各样式子行
  （`LayerStyleRowWidget`，眼睛开关单条效果）。对照 PS 图层面板缩进树，而非扁平勾选列表。
- **操作**：眼睛显隐、双击名称改名、不透明度滑条、新建（透明空层 / 0 瓦片）、删除、**复制图层**
  （右键 / **图层菜单** / `ImageDocument::duplicateLayer`：深拷贝像素与属性，插到源层上方）。
- **右键菜单**：对齐 PS 图层面板弹出项（**条目定义在 `layertreepanel.ui`**，多数灰显占位）；
  已接线：新建、复制、删除、重命名（行内编辑）、显示/隐藏。
  对照 GIMP：`layers-actions` + `layers_duplicate_cmd_callback` / `gimp_item_duplicate`。
  ⚠️ **尚无「上移/下移」**：`LayerStack::moveLayer` 已实现但**没有 UI 接线**
  （底栏与「图层」菜单都没有对应按钮/动作），`ImageDocument` 也还没有转发入口
  —— 直接动栈会绕过 `structureChanged` 与 Phase 6 的撤销收口，所以留到接线时一起做。
- **缩略图**：每行左侧显示该层像素的**等比缩略图 + 透明棋盘格衬底**（对齐 PS）；
  全透明层也显示棋盘格，所以「新建的空层」在列表里看得见。生成见 `ItemTreePanel::makeLayerThumbnail`。
  **Ctrl+点缩略图** → 图层 alpha 载入选区（再点同层取消）。
- **增量更新**：`structureChanged` 才重建列表；`activeLayerChanged` 只同步选中行；
  `layerPropertiesChanged` 只改受影响那一行 —— **画笔画一笔不会打扰面板的选中项与编辑态**。
- **缩略图防抖**：像素改动不立即重算，而是延迟 ~250ms **只重算活动层那一行**
  （画笔拖动时 `contentChanged` 每帧都发，同步重建会明显拖慢绘制；对齐 PS「停笔后才更新」）。
- **不透明度滑条**：拖动中只做百分比预览，**松手（或键盘操作）才提交**给 domain，
  且提交前做等值判断 → 一次操作 = 一次状态变更（为撤销铺路）。
- **类型筛选行**：按 PS 顺序的 5 个**图标**按钮（像素 / 调整 / 文字 / 形状 / 智能对象）。
  ⚠️ 本项目**目前只有像素层**（链接层仍属像素缓存），其余四类筛不出东西（调整层 Phase 8、完整 SO 未做），
  tooltip 已标「尚未实现」，**不做假的筛选逻辑**。
- **锁定行**：PS 四种锁的**图标**（锁定透明像素 / 图像像素 / 位置 / 全部）。
  ⚠️ 当前仅为 UI，尚未接入 domain（点选不会真的限制绘制）。
- **底栏按钮**：加大可点区域（约 30×30）；线框图标见 `resources/icons/layers/`（链接 / fx / 蒙版 / 调整 / 组 / 新建 / 删除），悬停有中文 tip。
  - **fx / 图层样式已接线**：底栏 fx 打开样式对话框；菜单「图层 → 图层样式」可追加投影 / 内阴影 / 外发光 / 内发光 / 描边 / 颜色叠加，或清除全部。
  - **实现**：`LayerStyleStack` 挂在 `Layer` 上（与 `FilterStack` 分离）；`LayerStyleEval` 在合成时非破坏求值（对照 GIMP DrawableFilter + PSD→`gegl:dropshadow` / `inner-glow` / `color-overlay`）。
  - **对话框**：每效果独立参数（颜色 / 大小 / 距离 / 角度 / 扩展 / 不透明度）。
  - **面板内效果树**：有样式时层行显示 `fx` 与展开箭头；展开后缩进列出「效果」与各效果眼睛开关（`setLayerStyleEnabled`）。
  - **工程**：`.pslite` 读写样式栈与图层蒙版；旧版本文件仍可打开（缺字段则按无样式/无蒙版处理）。
  - **未做**：斜面浮雕、渐变/图案叠加、样式预设拷贝、写入 PSD；「效果」组头一键全关。
- **图标来源**：整套由 `resources/icons/layers/_gen_svg_icons.py` 生成的 **SVG 矢量**，运行时按显示尺寸光栅化（任意尺寸锐利）。**自绘占位**，可按 `docs/ui/iconfont-icons.md` 的关键词从 iconfont.cn 同名替换。
- **限制**：无缩略图尺寸选项；
  底栏链接 / 调整层 / 图层组、填充滑条、图层筛选行仍为 UI 占位（**点了不会有反应**）。
  **蒙版底栏已接线**（无选区→显示全部/白；有选区→显示选区；再点删除）。行内显示蒙版缩略图；
  **添加后自动进入蒙版编辑**（蒙版缩略图高亮）；菜单「隐藏全部」等同。
  **单击蒙版**进入蒙版编辑（画笔写灰度：黑藏白显；橡皮涂白露出），
  **单击图层缩略图**切回像素；**Ctrl+点蒙版**载入选区，**Alt+点蒙版**启用/停用。
  从选区生成蒙版后会**取消选区**，否则选区继续裁剪画笔，黑区涂不上白。
  菜单：**应用**（烘焙进像素并删蒙版）、**链接/取消链接**（取消后移动层蒙版留在原处；编辑蒙版时移动可单独挪蒙版）。
  工程文件会把蒙版一并写入 `.pslite`。
  尚未做：蒙版专用通道面板、矢量蒙版。

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
| 画笔 / 橡皮 | 在活动层绘制或擦除 | 不污染其他层 | ✅（可撤销） |
| 选区 | 形状选区 + **魔棒/快速选择** + mask 蚂蚁线；填充后取消选区；**Ctrl+点缩略图**载入层 alpha | 填充只改选区内且事后无蚂蚁线；Ctrl+点建立外形选区 | ✅ |
| 清除 / 填充 | Delete 清选区或整层；Shift+F5 前景色填充 | 尊重选区；可撤销 | ✅ |
| 蒙版 | 灰度蒙版；合成 `alpha*=mask`；选区生成；涂画；应用/链接；可撤销 | 加蒙版后黑区隐藏；点蒙版缩略图用画笔涂；撤销恢复 | ✅ |
| 撤销 / 重做 | 绘制与图层操作 | 栈行为正确 | ✅ |
| 导出 | PNG / JPEG | 导出合成结果 | ✅ |

## 有余力可选（stretch，非完成标准）

规划见 `wiki/Roadmap.md` Phase 5；**不纳入**当前完善度验收。

| 功能 | 简介 | 状态 |
|------|------|------|
| PSD 导入 / 导出 | 常见分层 PSD 简版读写 | 可选待排期 |
| 智能对象 / 链接层 | 链接置入 MVP 已做；完整嵌入 SO / 监视器未做 | 链接 MVP ✅；完整 SO 待排期 |
| 插件库 | 极简扩展点（滤镜/导出钩子） | 可选待排期 |
