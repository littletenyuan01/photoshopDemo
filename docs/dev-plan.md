# 滤镜引擎性能与几何算子 —— 开发计划

> 本文把「异步/可取消定稿、中值增量 cum、ROI 通用化、位移算子」四条需求拆成**可单独验证**的开发步骤。
> 每条都写清：目标 / 验收标准 / 设计决策 / 实施步骤 / 关键文件 / 风险与踩坑。
>
> **现状**：①②④ 已落地（commit `70a9083`），③ 只落了第 ① 步（契约带几何），②③ 按决策后置。
> 本文既是已做事项的蓝本，也是后续续做的执行清单。演进记录见 [pending-dev.md](pending-dev.md)，
> 结构性结论见 [engine/roi.md](engine/roi.md)。

---

## 0. 四条需求一句话

| # | 需求 | 一句话 | 状态 |
|---|---|---|---|
| ① | 异步/可取消定稿 | 点确定/取消后整层滤镜求值**不占 UI 线程**、可中途取消、有进度 | ✅ 已落地 |
| ② | 中值增量 cum | 每像素扫 256 bin → 跨像素维护中值位置（O(r)），纯提速不改结果 | ✅ 已落地 |
| ③ | ROI 通用化 | 算子能回答「算这块输出要喂哪块输入」，且是**任意矩形** | ① 已落地，②③ 后置 |
| ④ | 位移算子 | 几何算子的第一个真实用例（对称外扩表达不了平移/环绕） | ✅ 已落地 |

---

## 1. 需求 ① —— 异步/可取消的滤镜定稿

### 1.1 目标与验收

| 目标 | 验收 |
|---|---|
| 重滤镜「定稿」不占 UI 线程 | UI 线程每帧只剩「取子图 + 混合」，实测 1600×1200 中值 r=3：**112 ms → 12 ms** |
| 可中途取消 | 协作式谓词真的被调用、结果是半成品被丢弃；结果与同步路径**逐像素一致** |
| 有进度提示 | 底栏一行「滤镜定稿：x%」 |

### 1.2 设计决策（为什么这么做）

| 决策 | 理由 |
|---|---|
| 主线程先**快照**（请求区 + pad 抽成独立 `QImage`），后台只读快照 | 工作线程**绝不**碰 `TileBuffer`/`Layer`（主线程随时写瓦片 = 数据竞争）；快照换来「不需要加锁」 |
| 后台按**横向条带并行**（`QtConcurrent::blockingMap`） | 整层求值原本在合成路径里就是并行的；单线程反而更慢（355 ms vs 58 ms） |
| 结果按**矩形**交层缓存（`Layer::setRegionFilterResult`），投影命中后只做取子图+混合 | 合成路径是同步的，但命中缓存后不再跑滤镜 |
| 取消两级：协作式谓词 + **世代号**（`Layer::contentGeneration`） | 谓词有延迟；世代号保证「过期结果**一定**写不进去」，不依赖信号时序 |
| `runId` 接替：每次 `start()` 自增，回调只认自己那一代 | 同一改动会连续触发两次 `contentChanged`，拒启会导致「第一个被取消、第二个没起来」→ 一次都不写 |

### 1.3 实施步骤与关键文件

```
1. FilterContext 加 shouldAbort（协作式取消谓词）          engine/op/filterop.h
2. FilterEval::applyNode / FilterStack::apply 加带取消的重载  engine/filtereval.*, domain/filterstack.*
3. 7 个面积算子在行循环里周期性查取消（每 32 行 / 中值每行）   engine/op/*filterop.cpp
4. 新增 LayerFilterJob（快照→条带并行→回主线程写缓存）        engine/layerfilterjob.*
5. Layer 加区域滤镜缓存 + contentGeneration                   domain/layer.*
6. CanvasView 在 contentChanged 时按「inputPad>0 且栈指纹变了」起后台任务，并轮询进度  ui/canvasview.*
7. 底栏状态条加进度标签                                       ui/canvasdocstatusbar.*
8. 测试 filterasync（协作式取消 + 缓存命中 + 世代号 + 端到端）  tests/t_filterasync.cpp
```

### 1.4 风险与踩坑（本需求真实踩过）

- **`markDirty` 会失效活动层缓存** → 回调里「先写缓存、后标脏」= 自己清自己。必须**先 markDirty 再写缓存**。
- **条带并行不能用 `QPainter` 写同一张图**（一个 paint device 一次只能一个 painter）→ 逐行 `memcpy`。
- 端到端断言才能抓到上面这类「单步都对、连起来错」的问题。

---

## 2. 需求 ② —— 中值增量 cum 定位

### 2.1 目标与验收

| 目标 | 验收 |
|---|---|
| 去掉每像素 × 每通道扫 256 bin 的常数项 | 受控交替 A/B：**2.80×**(r=3) / **4.44×**(r=1) / **1.93×**(r=8) |
| 结果**逐位不变** | 与**朴素排序中值 oracle**（r=1..4）逐像素 `diffs=0` |

### 2.2 设计（维护「中值 bin + 比它小的样本数」）

```
不变量：cum < half ≤ cum + hist[p]     （cum = 比 p bin 小的样本数，p = 当前中值 bin）

滑窗每格只让样本「进 2r+1 个 / 出 2r+1 个」：
  · 进 v < p → cum+1；出 v < p → cum−1
  · 再把 p 移回不变量成立处（上下各 ≤ 2r+1 步）
每行开头重建直方图时，顺手 O(256) 复位一次（每行一次，可忽略）。
```

### 2.3 关键文件

```
engine/op/medianblurfilterop.{h,cpp}   # 增量 cum 定位 + locateMedianByScan()（测试用朴素扫描）
tests/t_areafilter.cpp                 # oracle 扩到 r=1..4；成本实测
```

### 2.4 风险与踩坑（真实踩过 3 个）

- **环形槽取模必须夹紧**：`-2 % 3 == -2`，未夹紧直接读错行（直方图和 `65561/25` = 内容全错）。
- **abyss 计数口径必须守恒**：进出样本**各 (2r+1) 个**、越界一律按 0 照常计数，否则直方图和 35/25。
- **bin 0 回退要有下界**：中值恰在 bin 0 时 `--p` 会变 −1，越界写 `m_hist`（成片黑）。
- 教训：把「每像素重建答案」换成「跨像素维护状态」后，旧实现里无害的写法全变致命。

---

## 3. 需求 ③ —— ROI 通用化（三步）

### 3.1 目标与验收

| 步骤 | 内容 | 验收 |
|---|---|---|
| ① 契约带几何 | `FilterContext` 增 `srcRect / outRect`；两条路径填真值；直调路径保持「src = out = 图像自身」 | **七算子行为不变，全套测试绿** |
| ② 裁剪按几何 | `compositeRasterRegion` 改用 `FilterStack::requiredInputRect(want)`；裁剪点改为 `want.topLeft() - srcWant.topLeft()`；**图路径补齐「输出覆盖请求区」** | 测试仍绿 |
| ③ 算子契约改 `process(const QImage &src, QImage &out, ctx)` | 算子从 ctx 读映射、自己写输出 | 七处内环取样基准都要改 |

### 3.2 状态与决策

- ✅ 第 ① 步已落地：`srcRect/outRect` 一律**输入图本地坐标**；对外 `FilterStack::apply` 用**文档坐标**，换算收在 `PointFilterOp` 一处。
- ⏸ 第 ②③ 步**与「真的要做几何算子」绑定后置**。理由（写在 [roi.md §5](engine/roi.md)）：
  1. 全做完约 12 个代码文件（含 7 个当前正确的算子内环），收益只有「以后几何算子能装进来」；
  2. 做完仍装不下 **wrap-around**（多矩形依赖）；单矩形下 offset 用外扩逼近是 276 倍无用功；
  3. 栈里仍只能有一个几何算子且必须在最后。

### 3.3 若重启 ②③（执行顺序）

```
1. 先写约定：srcRect/outRect 的坐标空间，只允许一处换算（本次教训）          docs/engine/roi.md
2. ② 图路径补齐裁回请求区：PointFilterOp::process 在 p>0 时裁回 out.size()   engine/graph/pointfilterop.h
   → 验收：图路径输出 == 请求区大小，且与整图求值后裁同块 diffs=0
3. ③ 逐个算子改契约（一次一个，改完立即增量编译 + 跑 areafilter）：
   从「手里一张补边图 + 按 pad 取中心」改为「src + out + ctx 读映射」
4. 每改完一个，跑 areafilter 对应算子 + offset 用例，绿了再进下一个
```

---

## 4. 需求 ④ —— 位移算子（几何算子第一个用例）

### 4.1 目标与验收

| 目标 | 验收 |
|---|---|
| `out(x,y) = in((x-dx)%w, (y-dy)%h)`（wrap）/ 越界透明 | 与**朴素取模 oracle** 逐像素 `diffs=0` |
| 几何算子**整幅求值**、不参与逐块外扩 | **分块投影 == 整幅合成**（否则必在 64×64 块界错位） |
| 进同一滤镜栈（可开关/换序/撤销） | 栈被识别为「含几何算子」；往返保住 dx/dy/wrap |

### 4.2 设计（专用路径，不重建现有算子）

```
FilterStack::hasGeometryOp() ──► Layer::compositeRasterRegion 退化为「整层求值一次 + 裁请求区」
（与「有启用样式」走整层缓存同一类取舍）
```

### 4.3 实施步骤与关键文件

```
1. OpName 枚举 + 名字表 + isAdjustmentOp 排除            engine/op/opname.{h,cpp}
2. FilterNode 加 offsetDx/Dy/Wrap                       domain/filternode.h
3. OffsetFilterOp（另开结果图，逐行 memcpy）             engine/op/offsetfilterop.{h,cpp}
4. opsInit 注册                                         engine/op/opsinit.cpp
5. FilterStack::hasGeometryOp()                         domain/filterstack.*
6. Layer::compositeRasterRegion 加几何算子分支           domain/layer.cpp
7. .pslite 尾部追加 dx/dy/wrap                          io/projectio.cpp
8. 参数页（偏移滑块 + 环绕勾选）+ 菜单接线               ui/adjustmentpropshost.cpp, mainwindow.cpp
9. 用例 offset（oracle + 分块投影护栏 + 往返）           tests/t_offset.cpp
```

---

## 5. 通用开发纪律（本次教训的固化）

| 纪律 | 对应本次踩的坑 |
|---|---|
| **约定先行**：坐标空间/不变量先写进文档，再动第二个文件 | ROI 的 `srcRect` 文档坐标、`outRect` 本地坐标混用，Release 静默错 |
| **最小复现**：对不上先在最小用例复现，不做猜测性局部修补 | 连续 3 次改参考原点/期望/平移 |
| **断言前先打真值**：一次性 `qDebug` 把实际值打出来，再写期望 | `roictx` 期望值写错，误判实现有错 |
| **含中文文件只用编辑工具**（edit/write） | 两次用 PowerShell 改中文文件 → 乱码 + 字面量 `` `r`n `` |
| **先量后估**：构建成本/工作量先实测，不给拍脑袋数字 | 曾报「增量编译 2–4 分钟」，实测 3.2 s / 10.4 s |

### 5.1 构建成本（实测，用于后续估时）

| 操作 | 实测 |
|---|---|
| 改 1 个 `.cpp`（含链接） | **3.2 s** |
| 改 `filterop.h`（波及 filtereval + 7 算子 + pointfilterop 等） | **10.4 s** |
| 干净全量重建（test/app，16 核 `-j8`） | ~2–4 分钟 |

**结论**：纯「加一个邻域算子」= 8 步模板 + 一次增量编译 + 一次用例 ≈ **15–20 分钟**，不应以小时计。

---

## 6. 剩余项与建议次序

| 项 | 成本 | 建议 |
|---|---|---|
| ~~滤镜**预览**也异步/可取消~~ | 中 | ✅ **已落地**（第 15 批）：预览走静默标脏 + 专用信号复用 `LayerFilterJob`；UI 线程 225 ms → ~4 ms |
| 更多邻域算子（自定滤镜/最大值/最小值/高通/运动模糊 → 已落地；径向模糊/添加杂色待做） | 每个 15–20 min | 模板活，按需批量 |
| ROI ②③ | 大（改 7 算子内环） | 与「几何算子进栈中间」绑定 |
| offset 其它 abyss（CLIP/LOOP/BLACK/WHITE） | 小 | 顺手补 |
| 图层样式层仍走整层缓存（每笔可能整层重算，**未实测**） | 中大 | 先量一次再定 |

---

*维护：本文是规划型文档，随实现推进在对应小节勾掉「已落地」并回链到 [pending-dev.md](pending-dev.md) 的演进记录。*
