# 待开发 / 已知问题与完善方向

记录**已经有的功能**里，已知或可预见的问题，以及出现问题后建议怎么完善。  
不是功能清单（见 [features.md](features.md)）；这里只收「已实现但有债 / 有风险」项。

状态约定：

| 标记 | 含义 |
|------|------|
| 已实现 | 主路径可用 |
| 已知风险 | 特定条件下会暴露 |
| 完善方向 | 出问题或主动优化时的改法 |

---

## 移动工具拖图层 — 已缓解（2026-10）

**对照 GIMP**

- `gimp_viewable_preview_freeze` / `thaw`：拖中冻结缩略图等面板刷新
- `gimpeditselectiontool` live translate + `gimp_projection_flush`（异步分块，非每帧同步全合成）

**本项目落地**

- `ImageDocument::beginPreviewFreeze/endPreviewFreeze`：拖中抑制 `contentChanged` / `layerPropertiesChanged`，松手一次性 flush
- `MoveTool`：按下合成一次「跳过被拖层」底图；每帧只把该层叠回旧∪新区（`liveProjection`）
- `CanvasView`：拖中优先绘制 `Tool::liveProjection()`，避免每帧 `syncProjection` 全栈重合成

**相关代码**：`tools/movetool.*`、`domain/imagedocument.*`、`ui/canvasview.cpp`、`engine/compositor.cpp`（`blendLayerRange` + `skipLayer`）

---

## 自由变换（Ctrl+T）— 已实现

**现状（已实现）**

- 入口：编辑→自由变换 / Ctrl+T；选项栏；右键模式（自由/缩放/旋转/斜切/扭曲/透视）
- 交互态维护四角；预览写回同一图层瓦片（正常投影，z 序不变）
- 确认走 `PaintEngine::freeTransform` → `FreeTransformOp`（`quadToQuad` 逆映射采样）
- 插值：邻近 / 两次线性 / 两次立方；水平/垂直翻转；会话内逐步撤销
- 目标四角超出层 extent 时：`Layer::expandToIncludeLocal` 扩层，避免角被裁切

**对照**：交互偏 PS Ctrl+T；结构要点对照 GIMP Unified Transform（四角/确认再提交）。非完整对齐 GIMP（约束、GEGL 预览选项、路径/选区变换、clip 策略等见下「未排期对齐项」）。

### 已知风险：大层拖拽扩层可能卡顿

**可能出现什么问题**

- 旋转或拖大后，四角超出当前层宽高 → 触发扩层
- 扩层当前实现：`materialize()` 把**整层**已有瓦片拼成一张大图 → 贴进更大图 → `setFromImage`（`reset` 旧瓦片表再按新图重切）
- 大图、已分配瓦片多时，**每次**越界扩层都相当于整层「拆开再重装」→ 拖拽明显卡顿
- 卡顿主因是整层拷贝与重建，**不是**「多申请几块 64×64」本身

**已缓解（正确性，2026-10）**

- 进入 Ctrl+T 时快照整层像素+offset；**取消**时 `replaceFromImage` 整层还原（不再只 blit 源矩形）
- **提交**前先还原到进会话状态再 `pushLayerPixelsUndo`（含 offset）再扩层+栅格化，避免预览期 expand 泄漏进永久几何

**仍可能卡顿（性能）**：预览期每次越界仍可能 `expandToIncludeLocal`→整层重建；完善方向见下。

**出现问题后怎么完善（建议）**

参考 GIMP：变换在独立缓冲 / 图节点上算，预览改矩阵，确认再一次定稿；避免拖拽中反复整层 `materialize`。

可落在本工程的改法（任选或组合，单独开一轮）：

1. **会话工作缓冲**：进入 Ctrl+T 时按「最大可能包围盒」或按需扩一次工作 `TileBuffer`/`QImage`，拖拽只往工作缓冲写预览；确认再写回层（或替换层缓冲）。拖拽中不再 `setFromImage` 整层重建。
2. **轻量扩层 API**：扩 extent 时按瓦片格子平移/拷贝（只动边缘格），不要整层拼图再重切。
3. **拖拽节流**：预览用较快插值 + 限频；扩层合并到「松手 / 确认」再做（松手前可用临时画布预览越界部分）。
4. **观测**：大文档（如 4K 层）下对 `expandToIncludeLocal` / `updateLayerPreview` 打点，确认卡在 materialize 再改，避免过早优化。

**相关代码**

- `tools/transformtool.cpp`：`ensureLayerFitsCorners` / `updateLayerPreview` / `commitSession` / `cancelSession`（`m_preSessionPixels`）
- `domain/layer.cpp`：`expandToIncludeLocal`
- `domain/tilebuffer.cpp`：`materialize` / `setFromImage`
- `engine/op/freetransformop.cpp`：栅格化算子（卡顿主因一般不在此处采样循环，而在扩层重建）
- `app/undoitem.cpp`：`LayerPixelsUndo` 现含 offset

---

## 未排期：进一步对齐 GIMP 时可按模块补

若要以 GIMP Unified Transform 为齐，建议分轮，不必与上面卡顿优化绑在一起：

1. **约束** — Shift/修饰键与手柄约束（等比、沿边斜切、透视约束等）
2. **预览选项** — 是否显示预览、合成预览、同步、透明度等
3. **变换对象** — 选区 / 路径等，不只活动层像素
4. **clip** — 变换结果裁到画布 / 按结果调整层等策略 UI

---

## 维护

- 新发现「已有功能的坑」：在本文件追加一节（已实现 / 已知风险 / 完善方向 / 相关代码）
- 某项完善落地后：把该节改为「已缓解」并链到对应实现说明，或删除风险段并在 [features.md](features.md) 更新口径
