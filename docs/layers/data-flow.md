# 本项目实现对照：信号、脏区与数据流

概念见同目录其它页；本页对照 **当前代码实际怎么走**（已实现 / 仍简化）。

## 1. 改文档后的信号分级

`ImageDocument` 故意拆多条信号，避免「一改就整表重建」：

| 信号 | 何时发 | 典型订阅方 |
|------|--------|------------|
| `pixelsChanged(rect)` | 画笔等写像素 + `markDirty` | 画布（将来可只重合成 rect） |
| `layerPropertiesChanged(i)` | 显隐 / 不透明度 / 名 / 混合 | 图层面板只刷第 i 行 |
| `structureChanged()` | 增删 / 排序（层数或下标变） | 图层面板**重建列表** |
| `activeLayerChanged(i)` | 当前编辑目标变了 | 画布/面板更新选中态 |
| `contentChanged()` | 以上任一之后的汇总 | 状态栏等粗粒度刷新 |

约定：UI **不得**直接改 `Layer` 属性，一律走

`setLayerVisible` / `setLayerOpacity` / `setLayerName` / `setLayerBlendMode`；  
图层入栈唯一入口是 `addLayer`（内部挂 `owner`，否则属性信号不发）。

## 2. 新建图层（统一路径）

凡新建图层都是 **建层 →（可选）填充 → 入栈**；透明只是填充步骤不占瓦片内存。  
对照 GIMP：`gimp_layer_new` → `gimp_drawable_fill` → `gimp_image_add_layer`。

```
Layer(名, docW, docH)          // TileBuffer 预定格数
  → [可选] layer->fill(颜色)   // 透明新建：跳过；白底：fill(白)
  → ImageDocument::addLayer    // 挂 owner、structureChanged
  → （若需）setActiveLayerIndex / activeLayerChanged
```

| API | 填充 | 瓦片 |
|-----|------|------|
| `addTransparentLayer` | 无 | 0 块 |
| `createBlank` 内背景层 | `fill(白)` | 全覆盖格 |
| 打开图 `Layer(名, QImage)` | `setFromImage` | 按块拆入 |

面板入口：`layertreepanel` → `addTransparentLayer()`。

## 3. 一次「新建文档」链路

```
NewDocumentDialog 确认
  → documentSize() 得到像素宽高
  → ImageDocument::createBlank(w, h, 白)
       · new ImageDocument(w,h)
       · new Layer「背景」+ fill(白)   // 同上「建层+填充」
       · addLayer（挂 owner、structureChanged）
  → AppSession::setDocument(doc)
       · emit documentChanged
  → CanvasWorkspace / DockPanel / Colors… 订阅后换文档
  → Compositor::composite → CanvasView 显示
  → 切回工作区栈页
```

对照 GIMP：`gimp_image_new_from_template` + `gimp_create_display`；  
本项目无独立 Display 对象，主窗里固定 `CanvasView` 换文档即可。

## 4. 画一笔时发生什么

```
Tool → PaintEngine → OpRunner(OpName::StampDab) → StampDabOp
  → 写 activeLayer()->tiles()（瓦片窗口遍历，不整层物化）
  → 返回层内坐标脏矩形 → 工具层 translated 到文档坐标
  → ImageDocument::markDirty(rect)（累计 dirtyRect + contentChanged）
  → CanvasView::syncProjection → Projection::sync
       · dirty 对齐 64 chunk；小于全图 → Compositor::compositeRegion 就地重算
       · 否则 Compositor::composite 全量
  → 视图按 m_zoom 画 Projection::image()
```

**算子返回的脏矩形是投影重算范围的直接输入**：报小了不刷新（拖尾）、报大了白算。
完整调用链（含 `prepare/process/finish` 契约、常驻实例、坐标换算）见
[../engine/operators.md](../engine/operators.md)。

仍欠：分块有效位图、优先级渲染线程（当前是同步重算该脏区）。

## 5. 合成与视图（当前简化）

| 项 | 当前 |
|----|------|
| 层尺寸 / 偏移 | 层缓冲与文档同大；`Layer::offsetX/Y` 控制放置（移动工具改 offset） |
| 像素存储 | `TileBuffer` 64×64 懒分配；透明新建 0 块 |
| 混合模式 | PS 的 27 种全实现（枚举 + `engine/blend.cpp` + 面板下拉接线）；`Normal` 时退化为此前的 over |
| 合成范围 | 可传 `rect`；只混合已分配瓦片（按 offset 映射到文档） |
| 视图缩放 | `CanvasView::m_zoom`；≥4x 倾向关掉平滑，见像素块 |

## 6. 图层相关仍欠（相对概念文档）

| 能力 | 状态 |
|------|------|
| 新建（统一建层+可选 fill）/ 删除 / 显隐 / 改名 / 不透明度 | 已实现；面板新建固定透明填充 |
| 新建图层时选填充类型（白/前景色…） | 未做（GIMP 对话框有） |
| 上移 / 下移 | `LayerStack::moveLayer` 有，**UI 未接线** |
| 图层偏移 / 自由变换 | 偏移已实现（移动工具）；自由变换未做 |
| 图像大小 / 画布大小菜单 | 已实现（`.ui` 对话框 + `scaleImage` / `resizeCanvas`） |
| 文档内持久化 PPI | 未做（对话框仅换算用） |
| 撤销时 push 图层属性/结构 | **已做**（Phase 6：属性/结构/像素/文档几何） |
| 下方合成缓存、只重算脏块 | **已实现**（`engine/projection.*` + `Compositor::compositeRegion`，脏区对齐 64 chunk）；仍欠分块有效位图 / 优先级渲染 |
| 瓦片存储 / 透明层懒分配 | **已实现**（64×64 `TileBuffer`）；无 GEGL COW/scratch，见 [tiles-and-memory.md](tiles-and-memory.md) |

## 7. 关键代码

| 角色 | 路径 |
|------|------|
| 文档 | `psDemo/domain/imagedocument.*` |
| 图层 / 瓦片 / 栈 | `psDemo/domain/layer.*`、`tilebuffer.*`、`layerstack.*` |
| 合成 | `psDemo/engine/compositor.*` |
| 会话 | `psDemo/app/appsession.*` |
| 视图 | `psDemo/ui/canvasview.*` |
| 图层面板 | `psDemo/ui/layertreepanel.*` |
| 新建对话框 | `psDemo/ui/newdocumentdialog.*` |
| 主窗新建槽 | `psDemo/mainwindow.cpp` → `onNewDocument` |
