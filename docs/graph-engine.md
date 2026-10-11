# 图引擎（engine/graph）设计

> 【对照 GIMP】`app/gegl/`（`gimp-gegl-nodes.c` 建图 / `gimpapplicator.c` 子图复用）、
> `app/core/gimpfilterstack.c`（栈持 `GeglNode *graph`）、GEGL 本体（`GeglNode` +
> `GeglOperation` 的 `prepare/process/get_bounding_box`、ROI 反向传播、节点缓存）。
>
> 本页记录**本项目实际已实现**的图引擎；**不引入 GEGL**（自研演进，见
> [architecture.md](architecture.md) §8.1 主线 ③ 与 §9）。代码引用一律给**文件 + 符号**，不给行号。

---

## 0. 一句话

`engine/graph/` 是一套**函数式、无副作用的 DAG 求值引擎**：节点 = 算子 + 输入口 + 输出缓存 + 参数版本；
`PsGraph::evaluate(id, region)` 从需求区域反向传播 ROI、递归求值并缓存；
`CompositeGraph::build` 把图层合成（栅格层 / 调整层 / 蒙版）编成同一张图。

---

## 1. 为什么需要它：与 `engine/op` 的分工

GIMP 的核心洞见是——**图层混合、蒙版、滤镜、调整层全都是同一个 DAG 里的节点**。本项目因此并存
两套算子体系，职责不同，**不要混为一谈**：

| | `engine/op`（命令式算子） | `engine/graph`（函数式图算子） |
|---|---|---|
| 基类 | `BufferOp` / `PointOp` | `GraphOp` |
| 副作用 | **有**（写 `TileBuffer` / `Selection`） | **无**（`QImage` 进，`QImage` 出） |
| 触发方式 | 用户输入（鼠标/菜单） | 需求驱动求值 |
| 结构 | 无图，单次 `OpRunner::run` | DAG + 拓扑 + ROI + 缓存 |
| 典型 | `StampDabOp` / `FloodFillOp` | `PointFilterOp` / `BlendOp` |

- `engine/op` 解决「画一笔 / 选一块」这类**命令式编辑**（详见 [engine/operators.md](engine/operators.md)）。
- `engine/graph` 解决「把合成 / 滤镜这条**非破坏链**表达成可重排、可缓存、可按区域重算的图」。

---

## 2. 核心类型

### 2.1 图与节点

| 类型 | 职责 |
|------|------|
| `PsGraph` | 节点表（`id` = 下标）；`connect` / `validate`（输入口合法 + 无环）/ `topoSort`（Kahn）/ `downstreamOf` / `evaluate` / `bumpVersion` / `invalidateFrom` |
| `GraphNode` | `id` + 输入口（连向上游节点 `id`）+ `std::shared_ptr<GraphOp>` + 输出缓存（`QImage` + region + valid）+ 参数版本 |

数据流方向 = **上游 → 下游**；拓扑排序保证上游总在下游之前。

### 2.2 算子接口 `GraphOp`

```cpp
virtual int inputCount() const = 0;
virtual QVector<QRect> requiredInputs(const QRect &out) const = 0;       // ROI 反向传播
virtual QImage process(const QVector<QImage> &ins, const QRect &out) const = 0; // 纯函数
```

- 点算子：`requiredInputs(out) = {out}`（ROI 恒等）。
- 源算子：`inputCount() = 0`。
- 面积 / 变换算子可外扩输入区（**预留，暂未实现**，见 §8）。

### 2.3 图源 `GraphSource`

```cpp
virtual QImage read(const QRect &rect) const = 0;
```

源算子（0 输入）经它把外部数据（图层栅格 / 蒙版 / 纯色）接入图。持有方用
`std::shared_ptr<const GraphSource>`，保证源对象生命周期覆盖求值期。

---

## 3. 求值模型：ROI 反向传播 + 缓存 + 脏传播

`PsGraph::evaluate(id, region)`：

```
1. 缓存命中（同 region 且未失效）→ 直接返回缓存图
2. 否则 op->requiredInputs(region) 求各输入区
3. 递归 evaluate(上游, 各输入区) 取图
4. op->process(inputs, region) → setCache → 返回
```

脏传播两条入口：

| 入口 | 语义 | 何时用 |
|------|------|--------|
| `bumpVersion(id)` | 节点版本 +1 并失效其与**全部下游**缓存 | 该节点算子参数变更（`setOp` 后） |
| `invalidateFrom(id)` | 只失效不升版本 | 上游被重算后，通知下游别复用旧缓存 |

> **改参数的约定**：`graph.node(id)->setOp(newOp)` 替换算子后，必须再 `graph.bumpVersion(id)`，
> 否则下游缓存不会失效。

---

## 4. 具体算子

| 算子 | 输入 | 说明 |
|------|------|------|
| `ImageSourceOp` | 0 | 持 `QImage`，按区域裁剪返回（滤镜链的源） |
| `SourceOp` | 0 | 持 `std::shared_ptr<const GraphSource>`，读图源区域（合成图层源） |
| `PointFilterOp` | 1 | 持 `FilterNode`，调 `FilterEval::applyNode`；ROI 恒等 |
| `BlendOp` | 2（`backdrop`、`layer`） | 混合模式 + 不透明度 + 可选蒙版源；逐像素 `compositePixelOnto` |

合成图内部还有 `TransparentSource` / `LayerRasterSource` / `MaskSource` / `AdjustmentApplyOp`，
定义在 `compositegraph.cpp` 匿名命名空间（只被 `CompositeGraph::build` 使用）。

---

## 5. 合成入图（`CompositeGraph::build`）

`CompositeGraph::build(graph, doc)` 自底向顶建图，`sink` = 最后一个节点：

- **起点**：`SourceOp(TransparentSource)` 作透明累加器。
- **栅格层**：`SourceOp(LayerRasterSource)`（`ensureCompositeRaster()` 的图 + 文档偏移）
  → `BlendOp(mode, opacity, 蒙版源)`；`backdrop` = 累加器，`layer` = 层源。
- **调整层**：`PointFilterOp` 链（作用于累加器） → `AdjustmentApplyOp(opacity, 蒙版)`
  （`below` = 滤镜前累加器，`adjusted` = 滤镜后，逐像素 `lerp`）。

**逐像素合成被抽成 `engine/compositepixel.h` 的 `compositePixelOnto`**，现有
`Compositor::blendTileOnto` 与图 `BlendOp` **共用同一份**，保证两条路径逐像素一致
（对照 GIMP `gimpoperationlayermode-composite.c` 的 `composite_union`）。

`Compositor::compositeViaGraph(doc, rect)` = `build` + `evaluate`，供校验（见 §7）。

**坐标系**：`CompositeGraph` 的节点在**文档坐标**下求值（`LayerRasterSource` 按 `offset` 放置，
`BlendOp` 用 `out.left()/top()` 作 `Dissolve` 坐标）；`FilterStack::apply` 的链图在**层局部坐标**下求值
（整图、无 offset）。图内核本身坐标无关，约定由各 builder 决定。

---

## 6. 与 GIMP / GEGL 对照

| 本项目 | GIMP / GEGL |
|--------|-------------|
| `PsGraph` + `GraphNode` | `GeglNode` 图 / `GimpFilterStack` 的 `GeglNode *graph` |
| `GraphOp::requiredInputs` / `process` | `GeglOperation` 的 `prepare` / `process` / `get_bounding_box` |
| `GraphSource` | GEGL buffer source / `gegl:load` |
| `BlendOp` | `gimp:layer-mode` |
| `PointFilterOp` | `GeglOperationPointFilter` |
| `evaluate` 缓存 + `bumpVersion` / `invalidateFrom` | GEGL 节点缓存与脏传播 |
| `CompositeGraph::build` | `gimp-gegl-nodes.c` / `gimpprojection` 建图 |
| `compositepixel.h` 的 `compositePixelOnto` | `gimpoperationlayermode-composite.c` 的 `composite_union` |
| `FilterStack::apply`（链图求值） | `gimpfilterstack.c` 的 `graph` |

---

## 7. 验证

`graphSelfCheck()`（`main.cpp` 在 `opsInit()` 之后调用，失败仅 `qWarning`、不阻断启动）：

| 子检查 | 验证内容 |
|--------|----------|
| `checkStructure` | 菱形 DAG 拓扑序、下游集合、环检测 |
| `checkCache` | 求值缓存命中；`bumpVersion` 级联失效；参数变更后结果改变 |
| `checkCompositeGraph` | `Compositor::composite(doc)` == `Compositor::compositeViaGraph(doc)` 逐像素一致 |

本会话已用 qmake + `mingw32-make` 全量构建（`PSLite.exe`）并跑通全部自检。

---

## 8. 现状与仍欠（诚实）

**已实现**：DAG + ROI 反向传播 + 节点缓存 + 脏传播 + 滤镜链入图 + 合成入图 + 自检通过。

**仍欠（不得视为已完成）**：

| # | 欠账 | 说明 |
|---|------|------|
| 1 | **未接 Projection** | 生产合成仍走 `Compositor::compositeRegion`（命令式循环）；`compositeViaGraph` 是校验/演示路径，未被投影调用 |
| 2 | **图未持久化复用** | `FilterStack::apply` 与 `compositeViaGraph` 每次都 `build` 新图。**已实测**：只建图不求值（4 节点 + 3 连接）为 **0.0017 ms/次**，故持久化无可测收益、未做；跨帧缓存/脏传播仍未接入真实重投影。线性链上已改为**就地求值**（`GraphOp::canProcessInPlace`），省掉逐节点整图拷贝（3 节点省约 18 ms @2000×1500）。 |
| 3 | **合成源非稀疏** | 生产合成已改为**按区域**取层栅格（`Layer::compositeRasterRegion`：只 `extract` 请求块 → 只滤那块），稀疏优势在合成路径兑现；但**图路径**的 `LayerRasterSource` 仍用整层 `ensureCompositeRaster()`，且区域化目前只覆盖**纯滤镜层**（有启用样式的层退回整层缓存） |
| 4 | **无面积/变换算子** | `requiredInputs` 已留外扩点，但目前只有点算子（ROI 恒等）；模糊 / 位移等未做 |
| 5 | **异步 / 多线程**（原 Stage 4） | **按块并行已做**（在 `Projection::recomposeInvalidChunks`：`QtConcurrent::blockingMap` + 三处线程安全前置，含 `GraphOp::canProcessInPlace` 的独占交接）。注意它是**阻塞式**并行、不是异步；**异步求值 / 可取消求值仍未做**，图内也没有自己的调度器 |

---

## 9. 文件索引

| 文件 | 职责 |
|------|------|
| `engine/graph/graphnode.h` | `GraphNode`：节点 + 缓存 + 版本 |
| `engine/graph/psgraph.h/.cpp` | `PsGraph`：结构 + 拓扑 + ROI 求值 + 脏传播 |
| `engine/graph/graphop.h` | `GraphOp` 算子接口 |
| `engine/graph/graphsource.h` | `GraphSource` 图源抽象 |
| `engine/graph/pointfilterop.h` | `PointFilterOp` |
| `engine/graph/sourceop.h` | `ImageSourceOp` / `SourceOp` |
| `engine/graph/blendop.h` | `BlendOp` |
| `engine/graph/compositegraph.h/.cpp` | `CompositeGraph::build` + 内部源/应用算子 |
| `engine/graph/graphselfcheck.h/.cpp` | 自检（结构 / 缓存 / 图化合成一致性） |
| `engine/compositepixel.h` | `compositePixelOnto`：共享逐像素合成 |
| `domain/filterstack.cpp` | `FilterStack::apply` 走链图求值 |
| `engine/compositor.h/.cpp` | `blendTileOnto` 复用 + `compositeViaGraph` |
