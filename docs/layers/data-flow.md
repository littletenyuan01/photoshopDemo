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

## 2. 一次「新建文档」链路

```
NewDocumentDialog 确认
  → documentSize() 得到像素宽高
  → ImageDocument::createBlank(w, h, 白)
       · new ImageDocument(w,h)
       · new Layer「背景」+ fill(白)
       · addLayer（挂 owner、structureChanged）
  → AppSession::setDocument(doc)
       · emit documentChanged
  → CanvasWorkspace / DockPanel / Colors… 订阅后换文档
  → Compositor::composite → CanvasView 显示
  → 切回工作区栈页
```

对照 GIMP：`gimp_image_new_from_template` + `gimp_create_display`；  
本项目无独立 Display 对象，主窗里固定 `CanvasView` 换文档即可。

## 3. 画一笔时发生什么

```
工具写 activeLayer()->pixels()
  → ImageDocument::markDirty(rect)   // 累计脏区 + pixelsChanged
  →（当前）CanvasView 仍常全量 composite
  → 视图按 m_zoom 画到窗口
```

脏区接口已留（`dirtyRect` / `clearDirtyRect`），**局部只重合成**属 Roadmap Phase 7，尚未接到画布。

## 4. 合成与视图（当前简化）

| 项 | 当前 |
|----|------|
| 层尺寸 / 偏移 | 与文档同大、无 offset（属性面板 X/Y 为占位） |
| 像素存储 | `TileBuffer` 64×64 懒分配；透明新建 0 块 |
| 混合模式 | 枚举有，合成 v1 一律 Normal |
| 合成范围 | 可传 `rect`；只混合已分配瓦片 |
| 视图缩放 | `CanvasView::m_zoom`；≥4x 倾向关掉平滑，见像素块 |

## 5. 图层相关仍欠（相对概念文档）

| 能力 | 状态 |
|------|------|
| 新建 / 删除 / 显隐 / 改名 / 不透明度 | 已实现 |
| 上移 / 下移 | `LayerStack::moveLayer` 有，**UI 未接线** |
| 图层偏移 / 自由变换 | 未做 |
| 图像大小 / 画布大小菜单 | 未做 |
| 文档内持久化 PPI | 未做（对话框仅换算用） |
| 撤销时 push 图层属性/结构 | Phase 6，未做 |
| 下方合成缓存、只重算脏块 | Phase 7，未做 |
| 瓦片存储 / 透明层懒分配 | **已实现**（64×64 `TileBuffer`）；无 GEGL COW/scratch，见 [tiles-and-memory.md](tiles-and-memory.md) |

## 6. 关键代码

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
