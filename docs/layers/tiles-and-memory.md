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

## 4. 新建图层：同一条路，差别只在填充

**结论**：不管透明层还是白底/实色层，**新建图层都走同一套创建逻辑**；透明只是「还没（或不必）给瓦片分配像素内存」。

### GIMP（`layers-commands.c`）

```text
gimp_layer_new(宽, 高, 格式, 名字…)     ← 建层 + GeglBuffer(extent)，预定范围
    → gimp_drawable_fill(填充类型)        ← 透明 / 白 / 前景色…（唯一分叉）
    → gimp_image_add_layer(...)
```

- 填充=透明 → empty/zero tile，写时 unclone 才真正占块  
- 填充=实色 → 相关瓦片写入，立刻占内存  

### 本 Demo（**已实现**）

```text
Layer(名, w, h)           ← TileBuffer 只记 W/H 与格数（尚无瓦片块）
    → [可选] fill(颜色)    ← 透明：clearTiles / 不调；实色：ensure 全部格并填
    → addLayer(...)       ← 入栈、挂 owner、发 structureChanged
```

| 场景 | 调用 | 分配情况 |
|------|------|----------|
| 图层面板「新建」 | `addTransparentLayer` → `Layer` + 不 fill | **0 块**瓦片 |
| 新建文档白底背景 | `createBlank` → `Layer` + `fill(白)` | 覆盖范围**全部格**已分配 |
| 打开图片 | `Layer(名, QImage)` → `setFromImage` | 按块拆入（已有像素） |
| 画笔写透明层 | `stampDab(tiles)` | 只 ensure dab 碰到的格 |
| 合成 | `hasPixelData()` | 无块则跳过该层 |

`fill(透明)` 会 `clearTiles()`，与「逻辑全透明、释放块」一致。

**尚未做**：GEGL 式全局共享 zero-tile COW、scratch 盘、投影分块缓存（见 Roadmap Phase 7）；面板尚无「新建时选填充类型」对话框（目前新建层固定等价透明填充）。

## 5. 关键代码

| 角色 | 路径 |
|------|------|
| 瓦片缓冲 | `psDemo/domain/tilebuffer.*` |
| 图层 | `psDemo/domain/layer.*`（持有 `TileBuffer`） |
| 绘制 | `psDemo/engine/paintengine.*`（`TileBuffer` 重载） |
| 合成 | `psDemo/engine/compositor.*` |

## 6. 一句话

**瓦片 = 固定网格像素块；凡新建图层先同一套 `Layer(extent)`，再按填充决定是否占内存；透明=预定格数、写时分配；本 Demo 已按此实现，无 swap/共享空瓦片。**
