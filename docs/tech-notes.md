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

### 2026-09 — UI 结构收口（工具层 / 会话广播 / 信号分级）

> 完整审查记录见 `docs/ui-review.md`（含审查方法、依赖实测、仍欠清单、自测清单）。

- **新增 `tools/` 交互层**：`Tool` 基类 + `ToolManager` 注册表 + `Move`/`Hand`/`Zoom`/`Paint` 四个工具。
  `CanvasView::mousePressEvent` 里原先 5 条工具 if 分支全部移除；新增工具只需注册一行。
  【对照 GIMP】`app/tools/gimptool.c`（虚函数状态机）+ `gimptoolmanager.c`。
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
