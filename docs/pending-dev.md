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

## 移动工具拖图层 — 已对齐 GIMP Move（2026-10）

**对照 GIMP**（`gimpmovetool` → `gimpeditselectiontool`）

- motion：`gimp_image_item_list_translate`（真改 offset）→ `gimp_projection_flush`（脏区 + idle 分块）
- `gimp_viewable_preview_freeze` / `thaw`：**只冻缩略图**，不冻画布投影
- release：`gimp_image_flush` + thaw

**曾踩的坑（已改掉）**

- 把 `preview_freeze` 误做成「拖中不发 `contentChanged`、工具自建 `liveProjection` 底图+单层 blend」  
  → 那是**另一条捷径**，不是 GIMP Move；再叠加脏区沿轨迹累加，大图必卡。

**当前落地**

- `translateLayer`：旧∪新 bounds 记脏；冻住时仍发 `contentChanged` 驱动投影，抑制 `pixelsChanged`（缩略图）
- `MoveTool`：只 translate / shiftMask + freeze；无自建 live 缓冲
- `CanvasView`：`contentChanged` → `Projection::sync`（视口优先）+ idle chunk

**相关代码**：`tools/movetool.*`、`domain/imagedocument.*`、`ui/canvasview.cpp`、`engine/projection.*`

---

## 自由变换（Ctrl+T）— 已实现

**现状（已实现）**

- 入口：编辑→自由变换 / Ctrl+T；选项栏；右键模式（自由/缩放/旋转/斜切/扭曲/透视）
- 交互态维护四角；**拖中预览写入 `Layer::compositePreview` 工作缓冲**（不写瓦片、不扩层）；确认再一次 expand + 栅格化
- 确认走 `PaintEngine::freeTransform` → `FreeTransformOp`（`quadToQuad` 逆映射采样）
- 插值：邻近 / 两次线性 / 两次立方；水平/垂直翻转；会话内逐步撤销
- Compositor：有预览覆盖时叠「挖空后的瓦片 + 预览图」，z 序不变

**对照**：交互偏 PS Ctrl+T；预览语义对齐 GIMP composited preview 精简版（独立缓冲参与合成，确认再写 drawable）。未做：预览透明度选项、同步/异步开关、约束键等。

### 已缓解：大层拖拽扩层卡顿（2026-10，第 1 档）

**曾有问题**：预览每帧写回本层 → `expandToIncludeLocal` → 整层 `materialize`/`setFromImage`，大图拖角卡顿。

#### 逻辑优化对照（改前 → 改后）

| 维度 | 改前（写回本层预览） | 改后（会话工作缓冲） |
|------|----------------------|----------------------|
| **预览落点** | 每帧 `freeTransform` 直接写 `layer->tiles()` | 写临时 `TileBuffer`，结果挂 `Layer::compositePreview` |
| **层 extent** | 每帧 `ensureLayerFitsCorners` → 可能 `expandToIncludeLocal`（整层重分配） | 拖中**从不扩层**；仅 `commitSession` 扩一次 |
| **擦除旧预览** | 每帧 `clearLayerRect` 清旧脏∪源∪新目标，再重写瓦片 | 替换预览 QImage 即可；层瓦片在会话内保持「挖空后的真相」 |
| **文档真相** | 拖中瓦片已被改写；取消依赖整层快照回滚（含多次扩层后的尺寸） | 拖中瓦片不动（除进会话时一次挖空）；预览是合成覆盖 |
| **合成路径** | 与平常一样读瓦片（预览已在瓦片里） | Compositor：有预览时叠「瓦片（含挖空）+ 预览图」，z / 不透明度 / 混合不变 |
| **确认** | 再 `ensureLayerFitsCorners` + 正式 `freeTransform`（瓦片上已有预览痕迹，需先还原快照） | 清预览 → 还原进会话快照 → undo → **一次** expand → `freeTransform` |
| **取消** | 整层 `replaceFromImage` 回滚（可能已多次扩层） | `clearCompositePreview` + 还原进会话快照（extent 未被动过） |
| **代价模型** | O(扩层 materialize) × 拖拽帧数，大图拖出边界时爆炸 | O(目标包围盒栅格化 + 挂一张预览图) × 帧数；扩层成本摊到确认一次 |

**核心语义变化**：拖动阶段从「改 drawable 再靠 undo/快照假装可逆」改为「drawable 冻结 + composited preview」，对齐 GIMP「独立缓冲参与合成、确认再写 drawable」的精简版。

**当前做法（落地步骤）**

1. **会话工作缓冲**：`updateLayerPreview` 在临时 `TileBuffer` 上栅格化，结果挂 `setCompositePreview`；拖中不碰层 extent。
2. **确认一次定稿**：还原进会话快照 → undo → `expandToIncludeLocal` 一次 → `freeTransform` 写瓦片。
3. **取消**：`clearCompositePreview` + 整层还原进会话快照。

**仍可后置（收益递减）**：轻量扩层 API、拖拽节流、预览选项 UI。

**相关代码**

- `tools/transformtool.cpp`：`updateLayerPreview` / `commitSession` / `cancelSession`（已删拖中 `ensureLayerFitsCorners`）
- `domain/layer.*`：`setCompositePreview` / `clearCompositePreview`
- `engine/compositor.cpp`：预览覆盖叠层
- `engine/op/freetransformop.cpp`：栅格化算子

---

## 未排期：进一步对齐 GIMP 时可按模块补

若要以 GIMP Unified Transform 为齐，建议分轮，不必与上面绑在一起：

1. **约束** — Shift/修饰键与手柄约束（等比、沿边斜切、透视约束等）
2. **预览选项** — 是否显示预览、合成预览、同步、透明度等
3. **变换对象** — 选区 / 路径等，不只活动层像素
4. **clip** — 变换结果裁到画布 / 按结果调整层等策略 UI

---

## 维护

- 新发现「已有功能的坑」：在本文件追加一节（已实现 / 已知风险 / 完善方向 / 相关代码）
- 某项完善落地后：把该节改为「已缓解」并链到对应实现说明，或删除风险段并在 [features.md](features.md) 更新口径
