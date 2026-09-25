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

- 撤销：命令模式 vs 图层快照（或混合）
- 画布：`QWidget` 自绘 vs `QGraphicsView`
- 是否需要自有工程文件格式（含图层），或 v1 仅导出扁平图
- OpenCV / CUDA 的具体版本与可选编译宏命名

## 技术点日志

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
