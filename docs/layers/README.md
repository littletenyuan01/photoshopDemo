# 图层与文档 — 概念说明

本目录整理「文档 / 画布 / 图层 / 合成 / 视图」的概念，对应 domain + engine，  
与 `docs/ui/`（界面壳）分工：这里讲**数据与显示原理**，不讲菜单布局。

| 文档 | 内容 |
|------|------|
| [document-canvas.md](document-canvas.md) | 文档是什么、画布/视图、图像大小 vs 画布大小、分辨率 PPI |
| [layers-structure.md](layers-structure.md) | 层级结构、新建文档/图层生成什么、结构图 |
| [compositing.md](compositing.md) | 合成器、Alpha、预乘、混合公式、刷新策略 |
| [data-flow.md](data-flow.md) | **本项目**信号分级、新建/绘制链路、已实现与仍欠 |
| [tiles-and-memory.md](tiles-and-memory.md) | 瓦片、边缘不满格、step 含义、PS 懒分配 vs 本 Demo |

对照实现：

- 文档 / 图层：`psDemo/domain/imagedocument.*`、`layer.*`、`tilebuffer.*`、`layerstack.*`
- 合成：`psDemo/engine/compositor.*`
- 视图：`psDemo/ui/canvasview.*`
- 会话广播：`psDemo/app/appsession.*`
