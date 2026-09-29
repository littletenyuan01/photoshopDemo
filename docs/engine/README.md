# engine — 算法、算子与投影

本目录讲 `psDemo/engine/` 的**实现原理**：算子体系、调度链路、投影缓存。

与 `docs/layers/` 的分工：

| 目录 | 讲什么 |
|------|--------|
| `docs/layers/` | **数据模型与显示概念**：文档/图层/瓦片/合成的语义 |
| `docs/engine/` | **算法怎么被调用**：注册、调度、生命周期、脏区上行 |

| 文档 | 内容 |
|------|------|
| [operators.md](operators.md) | 算子调用链：注册表、常驻实例、三段式调度、脏区上行；缓冲/点两条路径的完整时序与示例 |

对照实现：

| 角色 | 路径 |
|------|------|
| 算子框架 | `psDemo/engine/op/`：`operation.h`、`bufferop.h`、`pointop.h`、`opregistry.*`、`pointopregistry.*`、`oprunner.*`、`opsinit.*`、`opcontext.h`、`oppad.h`、`opname.*`、`paintclip.h` |
| 缓冲算子 | `psDemo/engine/op/`：`stampdabop.*`、`floodfillop.*`、`gradientop.*`、`solidfillop.*` |
| 点算子 | `psDemo/engine/op/layermodeop.*`、`layermodecatalog.h`（+ 算法在 `engine/blend.*`） |
| 门面 | `psDemo/engine/paintengine.*` |
| 合成与投影 | `psDemo/engine/compositor.*`、`psDemo/engine/projection.*` |
| 驱动方 | `psDemo/tools/*`（画笔/油漆桶/渐变）、`psDemo/domain/imagedocument.*`（填充/清除）、`psDemo/ui/canvasview.*`（重投影） |
