# 瓦片与图层内存

本页说明大型编辑器（PS / GIMP）里常见的**瓦片存储**，以及本 Demo 的实现对照。

## 1. 瓦片是什么

**瓦片（tile）** = 一块固定大小的矩形像素区域。本 Demo 与常见引擎一致，边长为 **64**（`TileBuffer::kTileSize`）。

- 不是新建对话框里的「第二种画布尺寸」  
- 也不是视图缩放  
- 是引擎把大图**切块存储 / 按块更新**的方式  

一张文档宽高先定好；再按网格去切。长宽**不必**是瓦片边长的倍数。

## 2. 边缘不满一格

例：文档宽 160，瓦片 64 → 横向瓦片数 \(\lceil 160/64\rceil = 3\)。

- 文档有效宽度仍是 **160**  
- 本 Demo 边缘块直接存成 `min(64, 剩余)` 宽/高，**不**把文档加宽到 192  

坐标以文档**左上角 (0,0)** 为原点；瓦片原点按 0、64、128… 铺开。

## 3. 「step」别和瓦片边长混用

| 说法 | 常见含义 |
|------|----------|
| OpenCV `Mat::step` 等 | **一行跨度**（常含对齐 padding 后的字节数） |
| 补齐后的宽 | \(\lceil W/\mathrm{tile}\rceil\times\mathrm{tile}\)（如 192） |
| 瓦片边长 | **tile size**（本 Demo = 64） |
| 瓦片个数 | `tilesX` / `tilesY` = \(\lceil W/64\rceil\) 等 |

## 4. 新建文档 / 新建透明层（GIMP 与本 Demo）

GIMP：`gimp_layer_new` → `gegl_buffer_new(extent)` → `gimp_drawable_fill`；透明填充走 empty/zero tile，写时 unclone 才真正占块内存。

本 Demo（**已实现**）：

| 操作 | 行为 |
|------|------|
| `Layer(name, w, h)` 透明新建 | `TileBuffer` 只记 W/H 与格数，`allocatedTileCount()==0` |
| `fill(白)` / 实色 | 遍历全部格 `ensureTile` + 填色（白底背景占满覆盖范围） |
| `fill(透明)` | `clearTiles()`，释放全部块 |
| 画笔 `stampDab(TileBuffer)` | 只对 dab 覆盖格 `ensureTile` 再写入 |
| 合成 | `!hasPixelData()` 跳过；否则只混合**已分配**瓦片 |
| 打开图 | `setFromImage` 按块拆入 |

**尚未做**：GEGL 式全局共享 zero-tile COW、scratch 盘、投影分块缓存（见 Roadmap Phase 7）。

## 5. 关键代码

| 角色 | 路径 |
|------|------|
| 瓦片缓冲 | `psDemo/domain/tilebuffer.*` |
| 图层 | `psDemo/domain/layer.*`（持有 `TileBuffer`） |
| 绘制 | `psDemo/engine/paintengine.*`（`TileBuffer` 重载） |
| 合成 | `psDemo/engine/compositor.*` |

## 6. 一句话

**瓦片 = 固定网格像素块；透明层预定格数、写时分配；白底 fill 会占满覆盖块；本 Demo 已按此实现，无 swap/共享空瓦片。**
