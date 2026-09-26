# 开发路线

## Phase 0 — 工程与文档

- [x] Qt 空窗口工程
- [x] `.gitignore`
- [x] 项目 Wiki
- [x] `docs/` 技术文档骨架
- [x] Cursor 工程规则（文档同步 + commit 文案）
## Phase 1 — 能看见图

- [x] Document / Layer 数据模型（基础）
- [x] Canvas 显示合成结果
- [x] 新建 / 打开图像
- [x] 图层面板（列表、显隐、透明度、增删排序）

## Phase 2 — 能编辑

- [x] 画笔、橡皮（圆形 dab + 线段插值，写活动层）
- [x] 活动层绘制
- [x] **结构收口（留缝）**：信号分级 + 语义化 setter + 累计脏区（见 [docs/ui-review.md](../docs/ui-review.md)）
- [x] **附带收口**：抽出 `tools/` 工具层与 `app/AppSession` 广播（越晚做越贵的结构债）
- [ ] 撤销 / 重做 ← **下一步**（Phase 6，收口点已就位）

## Phase 3 — 链路闭合

- [ ] 导出 PNG / JPEG
- [ ] 菜单与快捷键整理
- [ ] 按 Wiki 自测整条演示路径

## Phase 4 — 简历打磨（可选）

- [ ] 截图 / 简短演示说明
- [ ] README 与 Wiki 对齐
- [ ] 控制总代码量，删掉无用实验代码

## Phase 6 — 撤销与命令层（架构核心 ①）

> **为什么不在最后做**：撤销是「改动前先推快照」的语义。若先做各按钮功能、事后补撤销，**每个改文档状态的入口都要回头改一遍**，且漏一处即静默不可撤销。故本相位必须**前置于按钮功能批量实现**。

> 【对照 GIMP】`app/core/gimpimage-undo-push.c`（46 KB）与 `gimpimage-undo-push.h` 的 **50+ 个 `gimp_image_undo_push_*` 入口**；每类对象一个 undo 子类（`gimpdrawableundo` / `gimplayerundo` / `gimpitemundo` / `gimpmaskundo` / `gimplayerpropundo` / `gimpundo.c`）。本项目取其**推入式 + 每对象一类**的语义，裁到最小集合；**不搬** GIMP 的 GObject undo 类层次规模。

> 【前置已就位】`ImageDocument` 的语义化 setter（`setLayerVisible/Opacity/Name/BlendMode`）已是唯一的属性变更入口，push 只需加在这里；不透明度滑条已改为「松手才提交」，保证一次操作 = 一次状态变更。见 [docs/ui-review.md](../docs/ui-review.md)。

- [ ] `push` 入口最小集合（先做这 4 类即可闭环）：
  - [ ] `pushDrawablePixels` — 像素改动（画笔/橡皮/滤镜），按脏矩形存快照
  - [ ] `pushLayerProp` — 显隐 / 不透明度 / 名称 / 混合模式
  - [ ] `pushLayerStructure` — 新建 / 删除 / 上移 / 下移 / 合并
  - [ ] `pushDocumentProp` — 尺寸 / 分辨率 / 活动层
- [ ] `HistoryStack`：undo/redo 双栈 + 内存上限（超限丢最老）
- [ ] **命令层收口**：所有改文档状态的 UI 动作统一走命令入口，入口内自动 push
- [ ] 菜单/快捷键接线：Ctrl+Z / Ctrl+Shift+Z，`编辑` 菜单项解除灰色
- [ ] 【约束】代码评审口径：**改文档状态而不 push = bug**

## Phase 7 — 投影与脏区分块（架构核心 ②，v1 后加强）

> 【对照 GIMP】`app/core/gimpprojection.c`（`update_region` / `priority_rect` / `iter` / `idle_id`）、`app/gegl/gimptilehandlervalidate.c`（603 行，脏区核心）、`app/core/gimpchunkiterator.c`，以及 GIMP 3.x 把 tile 实现下沉给 GEGL 后 GIMP 侧仅剩 91 行适配壳（`gimptilehandlerprojectable.c`）。

> 【本项目简化】不分块稀疏存储、不做优先级调度线程。只做「**脏矩形集合 + 分块缓存 + 按需重算**」。

- [ ] **模型与投影严格分离**：文档是真相；投影是只读缓存；`CanvasView` 只消费投影
- [ ] 文档级脏区信令：`dirty(QRect)` / `structureChanged()` / `activeLayerChanged()`
- [ ] `Compositor` 由「全量合成」升级为「按脏矩形 + 分块（如 64×64）缓存重算」
- [ ] 取消各处散落的 `update()` / 全量重合成，统一由脏区驱动
- [ ] 【成本约束】不引入后台渲染线程；保持同步，只减计算量

## Phase 8 — 节点化非破坏编辑（架构核心 ③）

> 【对照 GIMP】`gimpapplicator.c`、`gimpdrawablefilter.c`（94 KB）、`gimpfilterstack.c`、`gimpfilteredcontainer.c`，配合 `app/operations/layer-modes/`（17 个 GEGL op）——**滤镜 = 图层上的可重排、可带蒙版、可开关的节点**，而非一次栅格化。

> 【本项目简化】**不引入 GEGL**，只取其「滤镜是节点、可重排可开关」的语义，做成**只读滤镜节点栈 + 扁平的 `Layer`**。

- [ ] `Layer` **允许无可编辑像素**（调整层无自有像素；滤镜节点只声明参数与输入）
- [ ] 滤镜节点栈：增 / 删 / 重排 / 开关 / 参数
- [ ] 调整层：对「已合成的下方结果」应用（色阶 / 曲线起步）
- [ ] 投影管线支持节点求值；【前置】Phase 7 的脏区机制必须已在位
- [ ] 【硬约束】节点栈只读，不得就地改写 `Layer::pixels`

## Phase 5 — 有余力再做（可选 stretch，非 v1）

> 优先级低于 v1 闭环与简历打磨；**不作为完成标准**。体量大、兼容面广，仅在链路稳、代码量仍有余量时再开。

- [ ] **PSD 导入 / 导出**：能读常见分层 PSD（至少图层像素 + 显隐/透明度）；导出尽量保留图层。不追求完整 PS 特性兼容
- [ ] **智能对象**：图层可嵌入位图（或简化为「可再栅格化的嵌入文档」）；支持替换内容、统一变换后再栅格化参与合成
- [ ] **插件库**：极简扩展点（如滤镜/导出钩子），进程内动态库或脚本均可；**不做** GIMP PDB / 完整插件宿主
