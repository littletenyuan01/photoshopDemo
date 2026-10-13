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

## 缺陷台账（bug 也要留痕）

每个缺陷单独成节（现象含栈/复现步骤、根因、改法与被否掉的方案、防复发、仍欠）；这里只做索引。

| # | 现象 | 根因 | 防复发 | 详情 |
|---|------|------|--------|------|
| 1 | 分块并行合成后，含调整层的文档在性能用例里访问冲突崩溃 | `blendLayerRange` 调整层分支仍用 `QPainter` 画共享 paint device | 改逐行 `memcpy`；性能用例必须含调整层 | 「分块合成并行化」节 |
| 2 | 调整层数值框回车产生一条**空操作** undo | Qt spinbox 在 `interpret()` 时以 always-emit 重发**同值** `valueChanged` | `slider->value() == v` 守卫；`adjustpanel` 断言 | 「调整层参数持久化核对」节 |
| 3 | 滤镜对话框：**取消后**按 Ctrl+Z 把刚取消的滤镜**撤回来**；确定要按两次才干净 | 打开对话框就 push「添加滤镜」、取消又 push「删除滤镜」 | 临时节点 + `pushUndo` 参数；`filterundo` 用例 | 「滤镜接 UI」节 |
| 4 | 对话框内拖完参数按 Ctrl+Z **毫无反应** | 模态对话框吞掉主窗口的撤销快捷键 | 对话框自带 Ctrl+Z 逐步回退参数 | 「滤镜接 UI」节 |
| 5 | **点右上角退出软件崩溃**（`run_dtor_list → ~unordered_map`） | `thread_local` 容器跨调用持有 `QImage`；TLS 析构可能在 Qt 静态数据之后运行 | 实例表改「堆上不析构」+ 保留上限 128²；**规则 §12**；退出路径须实跑 | 「退出软件崩溃」节 |
| 6 | 查找边缘在半透明区又暗又钝（**写语义 oracle 时自捕获，未外发**） | 直接在**预乘**像素上求导、结果再写回预乘缓冲 = **二次预乘** | 补直通↔预乘两趟；oracle 断言钉住 | 「新增两个面积算子」节 |
| 7 | 面板选中第 2 行后「上移」仍是灰的（功能不可达，**测试提交前抓到**） | 按钮可用性只在 `refresh()` 里算，没连选中变化 | 抽出 `updateButtons()` + 连 `currentRowChanged`；`filterpanel` 断言 | 「图层滤镜栈面板」节 |
| 8 | 中值改增量 cum 后：中值落在 **bin 0** 的像素起，画布连续成片变黑（40×32 图连续 154 px 全 0） | 增量定位的回退循环**没有下界**：`cum = 0 < half ≤ cum + hist[0]` 时仍执行 `--p` → `p = -1` → 越界写 `m_hist` | `while (p > 0 && …)` + `Q_ASSERT`；`areafilter` 中值 oracle 扩到 **r=1..4** | 「中值：增量 cum 定位（第 9 批）」节 |
| 9 | 中值环形缓冲「同一个槽读出两行」：直方图和 ≡ 25 (mod 2¹⁶) 但内容全错（实测 65561/25） | 承 8 的改造里同时存在**两条**按行取槽的路径：`samp()` 用**未夹紧**的 `y` 取模（`-2 % 3 == -2`），直查用夹紧后的行号 → 建立直方图与滑窗更新读的不是同一行 | 只保留**一条** `rowAt(row)`；`QVector<QVector<QRgb>>` 改逐行 `append`（重复值构造会共享缓冲，且 `constData()` 恒等） | 同上 |
| 10 | 中值增量化的中间版本：直方图每像素和 35/25 并持续增长 | 越界（abyss）样本被写成「只加不减」：`qDel = delIn ? … : 0`，越界时 `--hist[0]` 被省掉 | 进/出样本**各 (2r+1) 个**、越界一律按 0 **照常计数**；注释写明这是口径守恒 | 同上 |
| 11 | 中值改增量 cum 的**首个**版本：环缓冲槽取模未夹紧，`-2 % 3 == -2` → 读错行，直方图减穿成 `65561/25`（和 ≡ 25 mod 2¹⁶ 但内容全错） | 同一行存在**两条**按行取槽的路径（`samp()` 用未夹紧的 `y`、直查用夹紧行号）；且 `QVector<QVector<QRgb>>(n, v)` 重复值构造让各行共享缓冲 | 只留一条 `rowAt(row)`；ring 逐行 `append` | 同上 |
| 12 | 中值改增量 cum 时**编辑失误**：`loadRow` 被误删 → 环缓冲前几行全 0，画面成片黑 | 一次「删探针块」的编辑同时删掉了紧邻的 lambda 定义与初始装载循环；**症状被误诊**为容器别名（多花了一轮） | 改后重跑 oracle（r=1..4）立刻暴露；探针与定义**分开**删 | 同上 |
| 13 | 异步定稿端到端「永远等不到缓存」：回调日志写成功（`covers=1`）后紧跟一条缓存失效 | 回调里**先写缓存、后 `markDirty()`**；而 `ImageDocument::markDirty` 会顺手 `invalidateCompositeRaster()`（连带清区域滤镜缓存）→ 自己把自己写的结果清掉 | 顺序改为**先 `markDirty` 再写缓存**，随后补发一次 `contentChanged` 让投影命中缓存 | 「滤镜定稿的异步/可取消（第 10 批）」节 |
| 14 | 异步定稿同上：一次都不写缓存（两个任务互相取消） | 同一改动连续触发两次 `contentChanged`；第二次 `start()` 因「上一个还在跑」返回 false，而它**已经把上一个取消** | `start()` 改为**接替**语义：每次自增 `runId`，回调只认自己那一代（旧任务自行作废），不再拒启 | 同上 |
| 15 | 后台条带并行结果与整层差 **24 万像素**（控制台刷 `QPainter::begin: A paint device can only be painted by one painter at a time`） | 多条带用 `QPainter` **并发**写同一张 `result` | 改**逐行 `memcpy`**（与分块并行合成同一修法） | 同上 |
| 16 | ROI 第①步落地时：`FilterContext` 的 `srcRect` 按文档坐标填、`outRect` 按本地坐标填，`applyNode` 的 `contains` 断言在 **Release 下不生效**，于是像素层求值实际算错一块 | 我在 `FilterStack::apply` 里对两者做了不同的平移（一个减 `srcRect.topLeft()`、一个减调用方的假定原点）；Debug 断言能挡住，Release 静默错 | 坐标空间**只用一套**：`FilterStack::apply` 的入参=文档坐标、`FilterContext` 一律本地坐标，换算收在 `PointFilterOp` 一处；`roictx` 用例把两条路径的几何真值钉住 | 「ROI 通用化第 ① 步」节 |
| 17 | 同上：`roictx` 首次跑出的期望与实际对不上，一度以为实现错 | **测试自己的期望值写错**（把文档坐标当成了本地坐标） | 断言消息里同时打印「期望/实际」两个矩形；先用一次性 `qDebug` 把真值打出来再写期望 | 同上 |
| 18 | 最小值初版 oracle 差 1008（= 整幅内部像素）；最大值却对 | `delIn=false` 时跳过「移除 abyss=0」→ 直方图里的 0 只进不出，整行最小值被拉成 0 | 进出样本各 (2r+1) 个、越界按 0 照常加减（与台账 #10 同一类坑，**oracle 提交前抓到**）；`morefilters` 断言钉住 | 「新增最大值/最小值/自定滤镜（第 13 批）」节 |
| 19 | 运动模糊 oracle 全部 `diffs=-1`（尺寸/格式不匹配），像素其实算对了 | 中间图是直通 `Format_ARGB32`，结尾直接 `image = work` **把管线约定的 `ARGB32_Premultiplied` 换掉了** | 结尾另建同格式结果图逐像素 `qPremultiply` 写回；`hpblur` 断言钉住（**测试提交前抓到**） | 「新增高通保留/运动模糊（第 14 批）」节 |
| 20 | 异步**预览**的区域结果整块像素全错（`diffs=10800` = 恰好区域面积）；而**定稿**用例照样绿 | 条带并行的取样/裁回公式**隐含假设 `outLocalLeft==outLocalTop==0`**：定稿时 want=整层，pad 被 `intersect` 裁剪吸收，公式退化后仍对；预览时 want=子区域，pad 是真实存在的，漏掉这两项 → 取到未外扩的列、裁回少偏一圈 | 通用式 `srcBand = band.translated(outLocal{L,T}).adjusted(±pad) ∩ snap.rect()`、`keep.topLeft = outLocal{L,T} + band.topLeft - srcBand.topLeft`；`filterpreview` 断言钉住（**测试提交前抓到**） | 「滤镜预览也异步/可取消（第 15 批）」节 |
| 21 | **堆损坏**（`0xC0000374` / `0xC0000005`）：单跑用例 30 次全过，**全量连跑**约 1/7 概率崩，而且崩在**别的**用例里 | `LayerFilterJob` 的后台 lambda 捕获了 `this`（写 `m_progress`、`QMetaObject::invokeMethod(this,…)`），但析构函数的注释却**声称「后台任务不持有 this」** —— 注释写了一个假不变量。对象先析构、任务还在跑 → 往已释放内存写 | 析构改为「先置取消旗标 → `m_future.waitForFinished()`」；新增 `jobteardown` 用例（反复「起重任务后立刻析构」）。注释里的不变量**是代码的一部分**，写错比不写更危险 | 「位置相关算子异步化 + ROI ②（第 18/19 批）」节 |
| **22** | ✅ **已修（T5.1）** — GPU 中值/最大/最小曾在 r=5..8 静默错像素 | **能力声明与 kernel 分离**：`isGpuOp` 只看 op 名；`median.comp` 的 `chR[81]` 只支持 **r ≤ 4**；host 原样传 1..8 | **`gpuParamsSupported` + `supports()` 校验 r≤4** ⇒ 越界走 CPU；`runMedianOnce` 双保险；`gpumedian` 钉住。通用后续见 T5.3 `maxParam` | `gpu-plan.md` §17.1 / T5.1 |
| **23** | 🔴 **第八轮**：`PSLITE_RASTER=gpu` 时**整个 GPU 后端一次都没生效过**（所有门控安静 SKIP） | `srcBuf` 用了 `QRhiBuffer::Dynamic` + `StorageBuffer`，QRhi **明确拒绝**（`StorageBuffer cannot be combined with Dynamic`）⇒ `newBuffer` 返回 nullptr ⇒ `tryCreate()` 失败 ⇒ 全部走 CPU。**而 `supports()` 直测用的是独立探测实例，照过** | 改回 `Immutable`（本就需要每次换一块，见 #25）；新增 `GpuRasterBackend::backendName()` 让门控**打印真实后端名**，并把「SKIP 不算通过 / 探测失败要能与不支持区分」写进 `gpu-plan §8/§9`；`gpukernels` 报告后端名（本机 = D3D11） | `gpu-plan.md` 第八轮复核 / §9 坑 7 |
| **24** | 🔴 **第八轮**：`BoxBlur` / `GaussianBlur` 上 GPU 后结果**放大 n 倍**（r=1 → 3×、r=3 → 7×、r=64 → 129×） | `boxblur.comp` 是一趟**二维**盒模糊（窗口面积 n²），归一化却写成 `(sum + n/2) / n` —— 只除了盒宽。CPU 是**两趟一维**（各除一次 n）⇒ 总面积 n²；一趟二维必须除 n² | 改 `area = n*n`、`(sum + area/2) / area`；`gpukernels` 新增 `BoxBlur r=1/3/64` + `GaussianBlur r=1/5/30` 门控（容差 ≤2 = 两趟/一趟舍入次数不同）。**教训：分离式算法的「等价」只在数学上成立，归一化因子是两趟的积** | `gpu-plan.md` T3.1 / §8 容差 |
| **25** | 🔴 **第八轮**：多算子链里**第二个算子读到的还是第一个算子的输出** | `Immutable` 缓冲在 QRhi 里**不能二次 `uploadStaticBuffer`**（D3D11 只安静跳过）⇒ 必须每次上传都换一块新缓冲。初版只在「需要时」重建 ⇒ 上传被跳过、srcBuf 里留着上一趟的结果 | `dispatchOnce` 结束置 `srcNeedsRecreate = true`；`ensurePipe` 判定「换了 shader 就连缓冲一起重建」。**单算子用例完全照不出来**（中值单测一直是绿的） | `gpurasterbackend.cpp` `ensurePipe`/`dispatchOnce` |
| **26** | **第八轮**：`noise.comp` 的幅度 `float(amount) * 2.55` 被 SPIR-V 翻译**改写**成 `amount / 256.0` | 翻译器做「代数等价」改写，数值不等价（相对误差 9.4e-7）。叠加 `noise.comp.qsb` 比 `noise.comp` 旧（改过源码没重烘焙）⇒ 跑的是旧逻辑 | shader 内改**整数**四舍五入 `(2*amount*255 + 100) / 200`（= 精确 round(amount·2.55)，已逐值验证 0..100）；残留 `amount ∈ {20,50}` 差 1（CPU 的 double 表示略低于 .5），落在护栏内并写进 §8 | `noise.comp` / `gpu-plan.md` §8 |
| **27** | **第八轮**：`custom.comp.qsb` 里只有 `DXBC 50`，**没有** `HLSL 50` | `.qsb` 是入库二进制，而生成命令在仓库里**任何地方都没有记录**；三个包的 target 集还各不相同（median/noise 用 HLSL、custom 用 DXBC） | 新增 `psDemo/tools/build-shaders.bat`（四个包同一组 target，唯一生成入口）；`gpukernels` 断言四个包都能解析且带 HLSL/GLSL 目标；`gpu-plan.md §9` 增「资产纪律」条目 | `psDemo/tools/build-shaders.bat` / `t_gpukernels.cpp` |
| **28** | **第八轮**：T5.4 的 ROI 回贴位算成 `outRect - srcRect.topLeft()` ⇒ **少贴一圈** | `image` 就是 `srcRect` 那一块（同坐标系），回贴位应当是 `outRect.topLeft()` 本身。实测 160×120 的 ROI 被贴到偏 (-60,-50) 处，**越权改了 11573 个 ROI 外像素**，而 outRect 内是旧值 | 回贴改 `drawImage(outRect.topLeft(), …)`；契约写进 `gpu-plan` T5.7；`gpukernels` 新增「outRect 外逐字节不变 + 请求区内确实被求值 + 非法几何拒收」三条。**初版门控的 oracle 也写错了**（拿忽略 srcRect 的 `CpuRasterBackend` 当参照）——oracle 也要单独验 | `gpurasterbackend.cpp` `runStack` / `t_gpukernels.cpp` |

---

## 移动工具拖图层 — 已对齐 PS 跟手 + GIMP 语义（2026-10）

**对照 GIMP**（`gimpmovetool` → `gimpeditselectiontool`）

- motion：真改 `offset`（`gimp_image_item_list_translate`）
- `preview_freeze` / `thaw`：**只冻缩略图**（`pixelsChanged`），不冻画布
- release：`gimp_image_flush` + thaw

**对照 PS 观感**

- 拖中：图层像素**连续跟着鼠标**移动（不等投影分块追上来）
- 松手：位置**立刻固定**，无「先消失再出现」

---

### 曾卡顿 / 松手闪：逻辑原因（改前）

移动卡顿不是单一 bug，而是**三条错误路径叠加**（按时间顺序）：

#### 路径 A — 每帧全量投影 sync（最初 GIMP 对齐版）

| 环节 | 做了什么 | 为何卡 |
|------|----------|--------|
| `mouseMove` | `translateLayer(..., emitContent=true)` | 每像素步进都改 offset |
| 文档 | 旧∪新 `styleBounds` 累计进 `dirtyRect`，发 `contentChanged` | 脏区沿拖拽轨迹**不断膨胀** |
| `CanvasView` | `contentChanged` → `Projection::sync()` | 对脏区内每个 64×64 块调用 `Compositor::compositeRegion` |
| 代价 | 大文档 × 多层 × 每 mouseMove 一次 | 4000×3000 级文档轻松 **<10 FPS**，图层「跟不上鼠标」 |

本质：**把「交互预览」当成「正式合成刷新」**，每帧重算多层混合，而不是只平移已算好的层像素。

#### 路径 B — `preview_freeze` 冻住画布 + 自建 live 但每帧整层 blend

| 环节 | 做了什么 | 为何仍卡 |
|------|----------|----------|
| `beginPreviewFreeze` | 拖中不发 `contentChanged` | 投影不更新，工具自建 `liveProjection` |
| `buildLive` | press 时全文档合成 below + above（**两次** `blendLayerRange`） | 按下就卡一下 |
| `mouseMove` | 每帧对脏区调用 `blendLayerRange(..., layerIndex, layerIndex+1)` | 仍走 Compositor 单层混合；有滤镜/样式/蒙版时每帧 `ensureCompositeRaster` |
| 图章 | 用裸 `materialize()`，无样式/滤镜 | 预览与最终画面不一致，且无法走快路径 |

本质：live 有了，但 **motion 仍在做合成器工作**，只是从「全栈 sync」换成「单层反复 blend」。

#### 路径 C — 投影分块预算 + live 松手无 adopt

| 环节 | 做了什么 | 现象 |
|------|----------|------|
| `Projection::sync` | 视口内限额 `kSyncPriorityBudget` 块/帧，其余 idle | 拖中若仍走 sync：视口内**一块块补洞**，图层位置滞后 |
| 松手 | 清 `liveProjection` → 再 `markDirty` + `sync` | 中间一帧画**旧投影**（层还在旧位置）→ 再重算 → **消失再出现** |

本质：**预览与投影缓冲没有无缝交接**；松手瞬间 live 没了、投影还没算完。

---

### 现不卡顿：逻辑说明（改后，2026-10）

核心思路：**拖中用「合成一次 + 图章平移」做预览；松手把 live 直接交给投影，不再重算。**

#### 逻辑优化对照

| 维度 | 改前（卡 / 闪） | 改后（跟手） |
|------|-----------------|--------------|
| **拖中谁负责画面** | `Projection::sync` 分块重合成，或 live 内每帧 `blendLayerRange` | `MoveTool::liveProjection()` 整幅 live 缓冲；CanvasView **跳过 sync** |
| **press 成本** | 两次全文档 `blendLayerRange`（below + above） | **无调整层上方**：复用 `projectionSnapshot`，只在层占位区重刷下方栈，above 烘焙一次。<br>**有调整层上方**：整幅建纯 below（不用快照），above **不**烘焙 |
| **motion 成本** | 脏区走 Compositor 叠当前层 | **图章快路径**：below 脏区 + `drawImage(stamp)`；无调整层上方时贴烘焙 above；**有调整层上方**时对补丁 `blendLayerRange` 重跑上方栈 |
| **层像素来源** | `materialize()` 裸瓦片 | `ensureCompositeRaster()`（含滤镜 + 图层样式 + origin 偏移） |
| **offset 变更** | `emitContent=true` 驱动投影 | `translateLayer(..., emitContent=false)`：只改 offset + 记脏，**不**触发投影 sync |
| **preview_freeze** | 曾误冻画布 | 只抑制 `pixelsChanged`（缩略图）；`endPreviewFreeze` 在 adopt **之前**调用，且 adopt 前 live 仍在 |
| **松手** | 清 live → sync 旧投影 → 重算 | `liveProjectionCommitted` → `Projection::adoptImage(live)`（块全标有效）→ 清 live；**零等待、无闪断** |

#### 路径 D — 上方有调整层时挖空区发白 / 未调整（曾 bug，2026-10 修）

| 改前错误逻辑 | 现象 | 改后 |
|--------------|------|------|
| `m_below = projectionSnapshot.copy()`（已含调整结果）+ 只在旧层矩形重刷下方 | 挖空区外仍是「旧投影」；挖空区内是未调整 below | 有调整层上方时 **禁用快照**，below = 纯下方栈 |
| `m_above` 在透明底上 `blendLayerRange` 烘焙，再 `SourceOver` 贴回 | 调整层滤的是透明，贴回**不会**对真实内容重跑调整 → 挖空处发白/未调 | `m_liveApplyAbove`：每帧对补丁 `blendLayerRange(live, layerIndex+1…)`，调整吃到 below+图章 |

#### 运行时序（改后）

```
mousePress
  ├─ beginPreviewFreeze()
  ├─ 若上方无调整层：below ← projectionSnapshot（层区重刷）+ above ← 烘焙
  │  若上方有调整层：below ← 全幅纯下方栈；above 空；m_liveApplyAbove=true
  ├─ stamp ← ensureCompositeRaster()（Normal 时预烘焙蒙版）
  └─ rebuildLive() → m_useLive = true

mouseMove（每步 dx,dy）
  ├─ translateLayer(index, dx, dy, emitContent=false)   // 真改 offset，不 sync
  ├─ patch = oldStamp ∪ newStamp ∪ oldBounds ∪ newBounds
  └─ rebuildLive(patch)
       ├─ 贴 below + 图章
       └─ 有调整层上方？ → blendLayerRange 上方栈（含调整）
                        ： → SourceOver 烘焙 above

mouseRelease
  ├─ endPreviewFreeze()
  ├─ emit liveProjectionCommitted(m_live)
  │    └─ CanvasView: m_projection.adoptImage(live); clearDirtyRect(); update()
  └─ clearLive()
```

#### 图章快路径适用条件

- 像素层（非调整层）
- 混合模式 = **Normal**
- 有蒙版时：press 时一次性 `applyMaskToStamp`，motion 不再走 Compositor
- **不满足**时回退：`blendLayerRange` 只重算脏区内的当前层（仍比全栈 sync 轻，但不如图章丝滑）
- **上方有启用调整层**：图章仍可用，但 above 必须每帧重跑（见路径 D）

#### 相关代码

| 文件 | 职责 |
|------|------|
| `tools/movetool.*` | below/above/stamp/live；`m_liveApplyAbove`；`liveProjection` / `liveProjectionCommitted` |
| `tools/toolcontext.h` | `projectionSnapshot`：press 时传入当前投影 |
| `engine/projection.cpp` | `adoptImage()`：松手无缝写入投影缓冲 |
| `ui/canvasview.cpp` | 拖中画 live 并跳过 sync；连接 `liveProjectionCommitted` |
| `domain/imagedocument.cpp` | `translateLayer(emitContent)`；`preview_freeze` 只冻缩略图 |

---

## 调整层拖参 / 显隐卡顿 — 改前 vs 改后对照（2026-10）

### GIMP 怎么做（对照）

GIMP **没有** PS 式常驻「调整图层」；等价物是 **drawable filter**（GEGL 算子图挂在 drawable 上）：

| GIMP | 含义 | 本项目对应 |
|------|------|------------|
| GEGL 节点缓存 | 参数变时只重算该算子，输入 buffer 复用 | 拖参时缓存 `below`/`above`，只重跑 `FilterEval` |
| `gimp_projection_flush` | 脏区 + 视口优先分块 | `Projection::sync` + idle |
| `gimp_projection_flush_now` | 同步刷完关心区域 | `Projection::flushPriority()` |
| 滤镜对话框预览 | ROI / 降采样，不全文档每 tick 全栈 | `adjustmentPreviewChanged` → CanvasView live |

本 Demo 按 **PS 调整层** 语义（独立层种 + 白蒙版 + 属性页），性能策略对齐「缓存输入、只重跑滤镜、主线程不堵滑条」。

---

### 曾卡顿：逻辑原因（改前）

用户体感常见两句：

1. **「滑条卡一会才动，动完画布才变」** → 主线程被合成堵住，事件循环转不动  
2. **「只有中间一小块先变，松手整图才变」** → 曾把预览裁成视口中心 ≤512 块（已废）

| # | 错误路径 | 做了什么 | 为何卡 / 错 |
|---|----------|----------|-------------|
| A | **拖参整幅 markDirty** | `setLayerFilterNode` → 调整层 extent≈画布 → 全文档块失效 | 每 tick 等价「整图重投影」 |
| B | **每 64×64 块重算滤镜** | `compositeRegion`：`dst.copy` + `FilterStack::apply`，无 input 缓存 | 同一 below 被反复滤 |
| C | **每块重混下方全部像素层** | 滑条一动 = 预算内全栈合成；idle 被打断 | 滑条与画布抢主线程 |
| D | **白蒙版慢路径** | `opacity≈1 && mask` 强制逐像素 `valueAt` | QPainter 整区快路径进不去 |
| E | **主线程 `ensureAdjustmentStacks`** | 定时器回调里同步 `blendLayerRange` 建 below/above（还曾分配**整文档**两张图） | **滑条 valueChanged 之后事件循环被堵** → 「滑条卡住再跳」 |
| F | **中心裁 512 预览**（中间态） | 为减负把 patch 裁成视口中心小块 | 只有一小块先变，松手才全图 sync |
| G | **显隐眼睛** | 大脏区 + 分块预算未 flush 视口就 paint | 先画旧块再一块块补洞 |

本质对比 GIMP：**把「改滤镜参数」当成「整图投影刷新」在 UI 线程做完**；正确做法是 UI 只改参数，重算进后台且复用 input。

---

### 现不卡顿：逻辑说明（改后）

核心思路：**主线程只改节点 + 合并定时器；建栈与滤镜全在 `QtConcurrent` 单飞；拖中缓存 below/above；覆盖整视口（可降采样算滤镜）。**

#### 总对照表

| 维度 | 改前（卡） | 改后（跟手） |
|------|------------|--------------|
| **滑条回调** | 间接触发主线程重合成 / 建栈 | 只 `setLayerFilterNode(..., pushUndo=false)` → `adjustmentPreviewChanged` → 启 32ms 单次定时器 |
| **谁建 below/above** | 主线程 `ensureAdjustmentStacks`（堵滑条） | **后台线程** `blendLayerRange`；主线程零合成 |
| **input 缓存** | 无，或每 tick 因 priority 微变重建 | 首次后台建好后拖参期间固定（`m_adjLiveLayer`）；视口变才丢 |
| **预览覆盖范围** | 曾裁中心 ≤512 → 小块先变 | **整视口** `priorityRect`；滤镜可在 ≤512 边长上算再放大回贴 |
| **滤镜算力** | 全分辨率点运算 / 无 LUT | `FilterEval` 模板内环 + 亮度/色阶 **LUT**；大图先缩小再滤 |
| **并发策略** | 无，或主线程串行 | **单飞**：busy 只记 `dirty`，结束再跑最新参数（不排队） |
| **松手** | markDirty + 分块 sync / 曾主线程全幅 rebuild | 清 ROI 缓存 → `syncProjection`（大脏区 `flushPriority` 视口） |
| **默认白蒙版** | 逐像素 mask | `isFullyOpaque` → 整区 Source 替换 |
| **显隐** | 视口分块未完就 paint | 脏区 ≥ 半幅文档 → `flushPriority()` 后再画 |

#### 拖参运行时序（改后）

```
QSlider::valueChanged
  └─ AdjustmentPropsHost::emitPreview
       └─ ImageDocument::setLayerFilterNode(..., pushUndo=false)
            └─ emit adjustmentPreviewChanged(layer)     // 不 markDirty、不 contentChanged

CanvasView::onAdjustmentPreview
  └─ 单次 QTimer(32ms) → flushAdjustmentPreview

flushAdjustmentPreview（主线程，必须轻）
  ├─ busy？ → m_adjPreviewDirty=true; return
  ├─ 有缓存？用 m_adjStackRect : 用整视口 priorityRect
  ├─ 浅拷贝 below/above 引用 + FilterStack 快照
  └─ QtConcurrent::run
       ├─ 无缓存：后台 blendLayerRange 建 below/above（ROI）
       ├─ 可选降采样 → FilterStack::apply → 放大回视口尺寸
       ├─ 叠 opacity/蒙版 + above
       └─ Queued：写 m_adjLive、缓存 ROI、update()；若 dirty 再 flush 一帧

sliderReleased → setLayerFilterNode(..., pushUndo=true)
  └─ adjustmentPreviewCommit → 清 live 缓存 → syncProjection（视口优先）
```

#### 为何滑条不再卡

| 改前堵点 | 改后 |
|----------|------|
| 定时器里主线程建两张全文档图 + 合成 | 建栈在 worker；主线程只投递任务 |
| 每 tick 全投影 sync | 拖参不发 `contentChanged`，不走 Projection::sync |
| 滤镜与滑条同线程 | 滤镜在线程池；滑条事件照常派发 |

#### 白蒙版 / 显隐（仍适用）

- `LayerMask::isFullyOpaque()` + Compositor 跳过逐像素蒙版  
- 大脏区 `flushPriority()`：视口立刻出图，避免旧块闪一下  

#### 相关代码

| 文件 | 职责 |
|------|------|
| `ui/adjustmentpropshost.cpp` | 滑条 → `previewChanged` / 松手 `commitChanged` |
| `ui/propertiespanel.cpp` | `setLayerFilterNode(false/true)` |
| `domain/imagedocument.cpp` | 调整层预览只发 `adjustmentPreviewChanged`；提交才脏区 + commit |
| `ui/canvasview.cpp` | `flushAdjustmentPreview` / 单飞 / ROI 缓存 / commit→sync |
| `engine/filtereval.cpp` | LUT + 模板点滤镜 |
| `domain/layermask.*`、`engine/compositor.cpp` | 白蒙版快路径 |

**已知限制**：首次建栈（大文档+多层）后台仍需时间，画布可能晚一两帧；已涂黑的蒙版走慢路径；平移/缩放会丢 below 缓存（下次预览重建）。

### 已修复：换层后画面「缺一截」、再点才恢复（2026-10）

**现象**：加任意调整层（不限色彩平衡）→ 点下方图层 → 再点另一图层时，画布上方（或局部）像少了一截；再点一次又正常。

**根因（两条叠在一起）**：

| # | 错误路径 | 后果 |
|---|----------|------|
| 1 | `contentChanged` 把 **调整层 `m_adjLive`** 和工具 live 一视同仁：有 live 就 `update()` 并 **跳过** `clearAdjustmentLive` + `syncProjection` | 换层也发 `contentChanged`；未松手的 ROI 预览或半成品 live 会卡住画面 |
| 2 | 新建调整层 → 整幅 `markDirty` → 投影分帧未完；换层时若只 `update`、idle 被拖参停过 | 未合成块仍透明/旧像素 → 「缺一截」；再点一次碰巧刷完 → 「恢复」 |

**改法**：

- `contentChanged`：**仅**工具 `liveProjection()` 跳过 sync；调整层 live 一律清掉再 sync。  
- 订阅 `activeLayerChanged`：清 adj live；若刚丢掉 live 则 `markDirty(视口)`；`flushPriority` + 确保 idle 继续。  
- `adjustmentPreviewCommit`：同样多刷一截 priority，避免清 live 瞬间露洞。  
- 属性面板：换层前若有 `pendingPreview`，对 **上一张** 调整层 `commit`（`m_boundAdjLayer`），避免参数已写入却无 undo、或 clear 丢掉提交。

**影响范围**：所有调整层种（共享 live / FilterStack 路径），非色彩平衡独有。

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

### 已缓解：拖中每帧投影重合成（第 2 档）

**曾有问题**：拖中 `updateLayerPreview` 每帧 `markDirty` → `contentChanged` → `Projection::sync()`
（分块重合成整个预览脏区），叠加栅格化后实测约 **2~5 FPS**。

#### 改后（照搬 MoveTool 的 live 图章思路）

- `TransformTool` 新增 `liveProjection()`：会话开始时用 `ctx.projectionSnapshot` 建
  「本层之下全文档合成」（先清空本层占位再叠下方栈）；拖中每帧把 patch 还原到 below，
  再从**本层到顶** `blendLayerRange`（含 `compositePreview`）。
- 画布 `contentChanged` 见 `activeLiveProjection()` 非空即**跳过** `syncProjection`
  （与移动工具同一短路，[`ui/canvasview.cpp`](../psDemo/ui/canvasview.cpp)）。
- 提交：先 `emit liveProjectionCommitted(m_live)`（→ `Projection::adoptImage`，避免回落到旧投影再重算），
  再 `clearLive()` + 正式 `freeTransform` + `markDirty`。
- 取消：`clearLive()` 后还原 —— 取消回到会话前状态，旧投影本就正确，无闪断。

**验证（无 GUI）**：断言 identity 与 scale-50 下 `liveProjection() == 提交后合成`，**diffs=0**。

#### 仍欠（实测已定位）

栅格化仍是最大单项：1400×1100 目标、2000×1500 源下实测
**Nearest 212ms / 双线性 418ms / 双三次 1035ms**。即 **>50% 的成本不在插值**，
而在 `TileBuffer` → `patch` → `TilePatch::blit` → `TilePatch::extract` 的往返与逐像素记账。

已试「仿射增量映射 + 去掉逐像素 `destPoly.containsPoint` + 不透明像素跳过除法」：
**A/B 交替实测无收益（差异全在机器噪声内），已回退**。
真正的下一步是**去掉中间缓冲直接写目标**，或并行化（不是改像素映射数学）。

---

## 投影刷新的时间预算（第 2 档）

`Projection::sync` / `flushPriority` / `processPendingChunks` 新增 `maxMs` 时间预算
（`sync` 的两阶段**共用同一时钟**，故总耗时 ≤ maxMs）；`CanvasView` 每帧给
`sync` 与 `flushPriority` 各 8ms，idle 每 tick 8ms。

**为什么**：旧行为里大面积脏（新建调整层、层级显隐）走 `flushPriority()` **无上限**
同步刷完整个视口 —— 实测单帧 **471ms**（2000×1500 / 6 层 / 视口 1200×800）。

| | 旧（无预算） | 新（8+8ms 预算） |
|---|---|---|
| 单帧最坏 | **471 ms** | **18.5 ms** |
| 稳态每帧 | — | **16.7 ms** |
| 收尾 | 同一帧内完成 | idle 分帧（163 帧 ≈ 1.5s 后台补完） |

代价：视口分 2~3 帧渐进填满，而非一次到位；换来 UI 不冻结。

---

## 合成器热路径优化（第 2 档）

两处**逐 chunk 固定开销**（受控交替 A/B：分块路径稳定 **15~25%**）：

1. `Compositor::compositeRegion` 的清屏：旧写法每 chunk 建一次 `QPainter` + `fillRect`；
   改为逐行 `memset`（预乘透明 = 全 0，等价）。
2. 栅格层瓦片遍历：旧写法 `forEachAllocatedTile` —— 遍历该层**全部**瓦片再逐个判交，
   分块重算时是 O(块数 × 层数 × 总瓦片数)；改用新增的**只读** `TileBuffer::forEachTileInRect(rect, fn)`，
   只走「与矩形相交且已分配」的瓦片格。

**行为等价验证**：`graphSelfCheck()` 的 `checkCompositeGraph` 用**独立的图合成路径**
交叉校验分块合成结果 —— 改动后仍 PASSED（逐像素一致）。

> ⚠️ **度量纪律**：本轮曾据两次非受控运行误判「memset 带来 2.3~2.6×」，
> 交替 A/B 后修正为 **15~25%**。本机单次运行噪声可达 ±2×，
> **凡性能结论必须来自交替 A/B（或多轮 min）**，不能拿前后两次运行直接相减。

---

## 分块合成并行化（第 2 档）

`Projection::recomposeInvalidChunks` 改为「收集候选 chunk → 分批 `QtConcurrent::blockingMap`
并行合成」，各 worker 只写投影缓冲的**不相交区域**。

### 三处线程安全前置（不做就是数据竞争）

| 前置 | 处理 |
|---|---|
| `Layer::ensureCompositeRaster()` 是 `mutable` 缓存 | 并行前在**主线程** `warmLayerCachesForParallel()` 预热；只预热**有启用样式**的层（纯滤镜层后来改为按区域求值、不碰该缓存，见下节），调整层不走这条 |
| `LayerMask::isFullyOpaque()` 是 `mutable` 缓存 | 同上预热 |
| `PointOpRegistry` 常驻 `LayerModeOp` 实例是**共享可变**（`setMode` 写 `m_mode`，两线程不同图层会互踩） | 合成器不再取实例：`blendTileOnto` 改收 **`BlendMode` 按值**；注册表只保留只读的 `find()` 校验 |
| 同一 `QImage` 被多线程 `scanLine` 触发 detach | 并行前 `(void)m_buffer.bits()` 确保独占 |
| `QPainter` 对同一 paint device 不可同时绘制 | 清屏改 `memset`；调整层「整区替换」改逐行 `memcpy` |

### 自适应批大小

一次 `blockingMap` 有数 ms 固定开销（唤醒 + 汇总），故按
`(时间预算 − 批次固定开销) / 单块耗时估计` 折算批量。单块耗时用 EMA，
且**只由「足够大的批次」更新** —— 否则小批次被固定开销主导，会把估计越推越高（正反馈）。

### 受控交替 A/B（16 逻辑核）

| 测量 | 串行 | 并行 | 提升 |
|---|---|---|---|
| 整幅脏 sync + 全部 pending | 482~800 ms | **105~121 ms** | ~4.6~7× |
| 摊帧到完成（含 idle） | 103~105 帧 / 1171~1200 ms | **15 帧 / 154 ms** | ~7.8× |
| 无预算到完成 | 564~609 ms | **60~64 ms** | ~9× |
| 预算单帧（8+8ms） | 16.7~24 ms | 17~20 ms | 持平 |

**行为等价验证**：`partest`（2000×1500 / 6 层 / 含 Multiply·Screen·蒙版）断言
「分块并行合成 == 单次整体合成」，**5 轮 diffs=0**。

> ⚠️ **本项由测试抓到真 bug**：并行后 `bench` 的调整层用例**访问冲突崩溃**，
> 报 `QPainter::begin: A paint device can only be painted by one painter at a time` ——
> `blendLayerRange` 的调整层分支还残留 `QPainter`。
> 而 `partest` / `transformtest` 因**不含调整层**全程未暴露。
> 教训：等价性测试的**分支覆盖不足会漏掉整条路径**；性能用例（含调整层）反而充当了崩溃探测器。

---

## 逐像素原语：Normal 快路径（第 2 档）

**先量后改**（`micropix` 微基准，102 万像素；ns/px）：

| 步骤 | 改前 | 改后 |
|---|---|---|
| 基线（纯读 src） | 0.7~0.9 | — |
| `unpremul` 不透明 / 半透明 | 1.8 / 6.4 | 未改 |
| `Blend::pixel` Normal（**跨 TU 调用**） | 7.5~9.6 | 未改 |
| **`compositePixelOnto` Normal 不透明** | **30~36** | **19.1** |
| `compositePixelOnto` Normal 蒙版 128 | 33.7 | 19.3 |
| `compositePixelOnto` Multiply 不透明 | 34.5 | 30.8 |

**根因**：`Blend::pixel` 定义在 `blend.cpp` —— 没有 LTO 时**跨 TU 不内联**；
而 Normal 下 `Blend::pixel` 恒返回 source，使合成公式里的 `(comp/255 − cs)` 项**恒为 0**，
编译器看不见、白算一遍。

**改法**：`compositePixelOnto` 为 `BlendMode::Normal` 加内联快路径，公式化简为
`ratio*(cs − cb) + cb`。

**精确性前提**（已实测校验）：`(n/255)*255 == n` 对 n∈[0,255] 全部成立，
且 `comp/255 == cs` —— 故化简是**位等价**，不是近似。

**等价性验证**：`pixeq` 穷举 **27 模式 × 56×56 像素对 × 3 不透明度 × 4 蒙版 × 2 坐标
= 2,032,128 组**，与「改动前逐字复制的旧实现」对比 → **0 处不一致**。

**端到端**（同机，与前面几轮可比）：

| 测量 | 改前 | 改后 |
|---|---|---|
| 全幅合成（5 层） | ~470~565 ms | **~324 ms** |
| 整幅脏 sync + 全部 pending | 105~121 ms | **73 ms** |
| 摊帧到完成 | 15 帧 / 154 ms | **8 帧 / 89 ms** |
| 无预算到完成 | 60~64 ms | **36 ms** |

**仍欠**：非 Normal 模式（Multiply 等）仍付 `Blend::pixel` 的跨 TU 调用（30.8 ns/px）。
可选后续：把 `blendPerChannel` / `pixel` 移入 `blend.h` 内联，或开 LTO ——
两者都会放大编译面与构建时间，需先量收益再决定。

---

## 图引擎就地链路（第 2 档）

**问题**：`FilterStack::apply` 走图求值时，**每个节点各持一份输出缓冲** ——
`ImageSourceOp::process` 的 `copy()` 加每级 `PointFilterOp` 的 `applyNode` 触发 detach 拷贝，
线性 3 节点链共 **4 次整图拷贝**（直接循环只需 1 次）。

**改法**：`GraphOp` 增加 `canProcessInPlace()` / `processInPlace()`；
`PsGraph::evaluate` 在「算子声明可就地 **且** 该输入只被本节点消费（`consumerCount()==1`）」时，
先放掉上游缓存持有的引用（使缓冲独占），再把栅格交给 `processInPlace` **就地改写**。
放缓存只损失一次可重算的缓存，不改变正确性；有分叉（多个消费者）时自动退回拷贝路径。

**受控 A/B（2000×1500，3 节点滤镜链，各 3 轮）**：

| | 走图耗时 |
|---|---|
| 无就地 | 475.8 / 493.7 / 473.3 ms（均 481） |
| **有就地** | 484.1 / 452.3 / 453.3 ms（均 **463**） |

→ 省下约 **18 ms**（即 3 次 12 MB 拷贝），与预估一致；每轮 `diffs=0`。

### 「图持久化」经测量判定为**不必要**

只建图不求值（4 节点 + 3 连接）实测 **0.0017 ms/次（1.7 µs）**，占 475 ms 的 **0.0004%**。
持久化不会带来可测收益，反而要为「源图随每次调用变化」引入额外状态（或被迫加一层
`setSource` 协议）。故**不做**，理由与数据记录于此。

> 这条是「先量后改」的正面样本：若按直觉去「持久化图」，会写一堆状态而收益约等于 0。

---

## 层复合栅格区域化（修掉一条 **447ms** 的无界停顿）

**问题（实测）**：在带滤镜的**像素层**上画一笔（64×64 脏区），UI 线程停 **447 ms**：

| 场景（2000×1500，某层带 3 个点滤镜） | 改前 | 改后 |
|---|---|---|
| 首次 `sync(预算 8ms)` | 416.6 ms | **12.4 ms** |
| 整幅脏（栅格已缓存） | 421.6 ms | **8.9 ms** |
| **64×64 小脏区（≈一笔）** | **447.2 ms** | **8.9 ms** |
| 全量排空（整幅重投影） | ~500 ms | **93.5 ms** |

链路：`painttool` → `Tool::markDocumentDirty` → `ImageDocument::markDirty` →
**清掉活动层的整层复合栅格** → `contentChanged` → 投影里 `ensureCompositeRaster()`
缓存未命中 → **整层 materialize + 整条滤镜链 + 样式**，全幅且**不受时间预算约束**。

**改法**：新增 `Layer::compositeRasterRegion(regionDoc)` —— 只 `TilePatch::extract`
请求区、只对那块跑滤镜；合成器改为按块（`area`）请求。点滤镜逐像素，故
「按区域滤镜」与「整层滤镜后裁剪」**逐像素等价**。同时把
`warmLayerCachesForParallel()` 收窄为「只预热**有样式**的层」，否则它会把
「整层一次建完」重新引回来。

**为什么区域化该排在异步之前**：异步只是把 447 ms 挪到后台 —— UI 不冻了，但该层
每帧仍滞后 ~0.45 s，画笔照样跟不上（吞吐没变）。区域化把工作量本身降到与脏区成正比，
并让工作可分块 → 时间预算才真正生效、并行才吃得上（全量排空 500 → 93.5 ms）。

**等价性验证**：`regionfeq` 构造「某层带 3 点滤镜 + Multiply」的文档，断言
「分块投影（区域化滤镜）」== 「`Compositor::compositeViaGraph`（整层栅格）」→ **diffs=0**。

**仍欠**：**有启用样式**的层仍走整层缓存（样式要外扩 halo，区域化须带 pad 再裁回），
故这类层仍可能有同类停顿（**未实测**）。可选解法：带 pad 的区域化样式求值，
或异步 + 双缓冲投影。

---

## 移动工具逐帧路径并行化（真实文档实测）

**问题**：`演示2.pslite`（2480×3508，8.7 Mpx）里 tempGirl 层上方有「图层 4」+ 两个调整层。
关掉调整层时移动很跟手；**打开后每帧卡**。

**根因**（`tools/movetool.cpp` 的 `rebuildLive`）：上方有调整层时 `m_liveApplyAbove == true`，
每帧必须重跑**上方全部层**（调整层依赖移动后的下方结果），而这条调用是**单次串行**
`Compositor::blendLayerRange` —— 并行只加在 `Projection` 里，**工具层绕过了它**。

**改法**：新增 `Compositor::blendLayerRangeParallel()` —— 把 rect 切 64×64 块交
`QtConcurrent::blockingMap`；各块只读写自己那块 dst 区域（栅格层读层瓦片、调整层
`dst.copy(area)` 后写回同区），互不相交。`MoveTool` 的逐帧与按下路径改用它。
顺带把预热逻辑上提为 `Compositor::warmLayerCaches()`（原 `Projection::warmLayerCachesForParallel`）。

**实测（同一文档、同进程交替 A/B）**：

| | 串行 | 并行 | 提升 |
|---|---|---|---|
| 每帧上方层合成 | 30.2 ms (33 fps) | **3.5 ms (283 fps)** | **8.5×** |
| 端到端每帧（贴 below 补丁 + 贴 stamp + 上方层） | 71.2 ms (14.0 fps) | **13.7 ms (73.1 fps)** | **5.2×** |

**等价性**：串行 vs 并行整幅合成 **diffs=0**。

**仍欠**：端到端剩下的 ~10 ms 是两处 `QPainter::drawImage`（贴 below 补丁 / 贴 stamp）；
要再快就得把它们换成分块 `memcpy` 并与上面一起并行。

---

## 调整层参数持久化：核对结论是「本来就对」，但补掉一个真实缺口

用户报告「重新打开文档后滑动条回到 0」。逐环节核对（用其真实文件 `演示2.pslite`）：

| 环节 | 结论 | 证据（本轮新增的验证程序） |
|---|---|---|
| 文件里有没有参数 | **有** | `dumppslite`：亮度/对比度 `bri=0.08 con=0.10`；色阶 `levels=137/168/4.79` |
| 存读往返 | **OK** | `roundtrip`：kind + 全部参数逐项一致 → `ROUNDTRIP OK` |
| 回填到滑动条 | **OK** | `adjui`（离屏实例化面板）→ 8 / 10 / 137 / 168 / 479 |
| 重开后是否作用到画面 | **是** | `adjapply`：关掉两个调整层前后，视口 600×600 有 **99.17%** 像素不同 |

所以「参数丢失」在当前代码里**不复现**。但核对中发现一个**真实缺口**并已修：
`AdjustmentPropsHost` 的提交信号只挂在 `QSlider::sliderReleased` —— 在**数值框里输入/回车/
滚轮**改值时只走预览，**不产生 undo、也不把文档标记为已修改**，关窗时可能不提示保存，
用户的改动就丢了。

**改法**：加 `m_pendingPreview` 标记「有未提交的预览」；`sliderReleased` 与数值框
`editingFinished` 都调 `emitCommit`，无待提交改动时不提交（避免重复 undo 条目）。

**验证**（`adjui` 离屏）：改数值框 → `preview+1 commit+0`；回车 → `preview+2 commit+1`。

> 两条测试自身的坑（记下来免得再犯）：
> ① `host.findChild<QSlider*>("slider_gamma")` 会**全树**搜索 —— 色阶页与曝光页同名，
> 必须按「当前页」查找，否则测出假值；
> ② 信号 lambda 若每轮重新 `connect` 且按引用捕获**局部**计数器，旧 lambda 会写悬垂引用 →
> 计数翻倍。要么只连一次，要么按增量断言。

---

## 面积算子 BoxBlur + ROI 外扩（滤镜库起步）

**背景**：此前所有滤镜/调整都是点算子（ROI 恒等）；`GraphOp::requiredInputs` 的外扩分支设计好了却从没被用过 —— 模糊/锐化/杂色这类「邻域算子」一个都没有。

**落地（首片）**：

- `OpName::BoxBlur` + `FilterNode::blurRadius`（含 `inputPad()`）+ `FilterStack::totalPad()`。
- `FilterEval::applyBoxBlur`：**直通 alpha** 的可分离盒模糊（两趟滑动窗口），abyss = 透明（层外即透明）。
- 图路径：`PointFilterOp::requiredInputs` 按 `inputPad()` 外扩、`process` 求值后裁回 `out`（面积算子 `canProcessInPlace()==false`，走常规拷贝路径）。
- 像素层滤镜（生产路径）：`Layer::compositeRasterRegion` 取「请求区外扩 totalPad」再求值、裁回。
- `.pslite` 增存 `blurRadius`（仍在 512B 预留块内）。

**验证（常驻用例 `areafilter`，12/12 通过）**：

- 图路径 BoxBlur == 直接 FilterEval（diffs=0）—— ROI 外扩/裁回真走通；
- **像素层 BoxBlur 分块投影 == 整幅合成（diffs=0）—— 跨 64×64 块无接缝**（面积算子最易错处）；
- 半径变化改变结果；BoxBlur 非调整层算子（护栏）；往返保住 radius。

**关键结论（为什么先做面积算子而非菜单）**：这是结构上真正缺的一块；且 `compositeRasterRegion` 读的是**层自己的瓦片**、不碰共享投影缓冲，pad 天然无接缝 —— 像素层面积算子这一步是**安全且正确**的。

### BoxBlur 内环与临时缓冲（**先量后改**）

**基线（改前）**：

| 场景 | applyNode | 两次触页分配 | 分配占比 |
|---|---|---|---|
| 64×64 r=2（每块） | 0.084 ms | 0.019 ms | 22.4% |
| 256×256 r=4 | 1.165 ms | 0.015 ms | 1.3% |
| 1024×1024 r=8（整层级规模） | 42.237 ms | 2.228 ms | **5.3%** |

**量出来推翻了我的假设**：我原打算按「三段式 + 常驻实例复用临时缓冲」省分配，但**在最吃时间的大块场景，分配只占 5.3%** —— 真正的开销在卷积内环本身：

1. 每像素 **4 次整数除法**，除数 `n = 2r+1` 是运行时值（约 20~40 周期/次）；
2. 垂直趟每样本调一次 `QImage::pixel()`（逐样本格式分发 + 边界检查），半径 8 时约 19 次/像素。

**改法**：

- 除法 → **倒数乘法**（`((sum + n/2) * inv) >> 32`）。在本项目取值范围内（sum ≤ 65k、n ≤ 257）该近似**精确**，已由语义 oracle 证实与整数除法逐像素一致。
- 垂直趟 → 预先取**行首指针**（`constScanLine` / `scanLine`），消除逐样本 `pixel()` 与 `scanLine()` 的 detach 检查。
- 临时缓冲 → **线程本地池**（`thread_local AreaBufferPool`，只缓存最近尺寸、上限 1 Mpx ≈ 4 MB/块；超限退回「用后即弃」）。用 `thread_local` 是因为并行分块下同一算子会被多个工作线程同时调用，全局静态会数据竞争。

**结果（累计）**：

| 场景 | 基线 | 现在 | 提升 |
|---|---|---|---|
| 64×64 r=2（每块） | 0.084 ms | **0.039 ms** | **2.15×** |
| 256×256 r=4 | 1.165 ms | **0.629 ms** | **1.85×** |
| 1024×1024 r=8 | 42.237 ms | **23.914 ms** | **1.77×** |

**垂直趟访存局部性（已做，含一次「自适应」修正）**：1024² 一度是 23.9 ms（≈11.4 ns/像素趟），确认瓶颈是垂直趟按列扫的访存（每像素跨 17 行、每行跨 4 KB）。

改成**按列块**（64 列一组、行在外，4 组 running sums 常驻 L1）后 1024² 降到 ~15~18 ms，但**小图反而变慢近 2×**（64×64 块 0.039 → 0.074 ms）—— 因为分块版每像素要读写 4 个 running-sum 数组（每像素成本更高），而小图的源数据本来就进 L1，标量累加版就赢。

故改为按**图高自适应**（`h > 512` 才走列块）：L1 论据（逐列扫的工作集 ≈ h×64 B，超过 ~32 KB 即 h > 512）与实测拐点一致 —— 小图回到最快、大图拿到局部性收益。

| 场景 | 基线 | +内环修复 | +缓冲池 | +自适应列块 |
|---|---|---|---|---|
| 64×64 r=2（每块） | 0.084 ms | 0.052 | 0.039 | **~0.039** |
| 256×256 r=4 | 1.165 ms | 0.741 | 0.629 | **~0.69** |
| 1024×1024 r=8 | 42.237 ms | 26.257 | 23.914 | **~15~18** |

> ⚠️ 本机单次运行噪声大（**同一二进制**跑 64×64 三次得 0.037 / 0.057 / 0.039 ms，±50%）：上表小图后两列之差（0.629 vs 0.69）在噪声内，**不作为结论**；结论只取量级差异。

**仍欠（下一轮）**：

- ~~**调整层 + 面积算子**~~ ✅：合成器 `blendLayerRangeImpl` 对 `requiredInputRect` 外扩走**私有子缓冲**
  （下方层叠到 srcWant、滤镜后只写回请求区）；点算子仍读投影本块。护栏改为只挡 Offset / 位置相关。
  用例 `adjarea`。
- 更多面积算子：中值、锐化（USM）、Sobel、位移（参考 GIMP `gimpoperationoffset.c`）。
- abyss 策略固定为「透明」；GEGL 有 CLIP / LOOP / BLACK / WHITE / TRANSPARENT，后续可参数化。

---

## 滤镜算子族 FilterOp：统一「有资源滤镜」的生命周期（第 2 档）

**为什么改**：此前滤镜求值走 `FilterEval::applyNode` 的**自由函数 switch**（直接调用、无生命周期），
与绘制类走 `BufferOp`/`OpRunner` 的**三段式**是两条互不相干的壳。面积算子（如 BoxBlur）需要
整块临时缓冲，没有三段式就只能每次 new —— 我上一轮用 `thread_local` 池临时顶住（见上节），
但那只是「复用」，不是「申请/释放有归属」的生命周期。Roadmap 9.0.1-1 就是要收这个口。

**改法**：新增**第三族算子 `FilterOp`**（`engine/op/filterop.h`）：

| 要素 | 决定 | 为什么 |
|---|---|---|
| 另立一族，而非并入 `BufferOp` | 滤镜作用在**临时 QImage**、不写回层瓦片 | `BufferOp` 的 `OpContext` 装的是 `TileBuffer`/`Selection`，硬塞一族会让它既装瓦片又装图 |
| 生命周期 | `prepare → process → finish`，`finish` 无条件调用 | 与 `BufferOp` 一致：`prepare` 申请/校验、`finish` 释放「不该跨调用保留」的资源（≈ finally） |
| 常驻实例 + finish 的分工 | 固定尺寸的临时缓冲是**实例状态**（跨调用复用）；**超出保留上限**的大缓冲在 `finish` 放掉 | 常驻的意义就是复用（对照 GEGL buffer backend）；但要防大图在 16 线程下白占内存 |
| 线程协议 | **每线程一份实例**（`thread_local` 表），不加锁 | 并行分块会多线程并发调用同一算子；共享可变状态是并行崩溃的根源（前例：`LayerModeOp` 的共享 `m_mode`） |
| 点滤镜 | **不**注册为 `FilterOp`，仍走 `applyNode` 直接分发 | 无临时资源，三段式纯属开销（与 `PointOp` 同一取舍） |

`BoxBlur` 迁入 `engine/op/boxblurfilterop.{h,cpp}`（算法原样搬移，临时缓冲改由 `prepare` 持有、
`finish` 依 `m_oversized` 释放）；`FilterEval::applyNode` 变为「有 FilterOp 实例 → 三段式；否则 → 点滤镜直接分发」。

**验证**：`areafilter` 新增两条断言 —— `BoxBlur` 已注册为 FilterOp、同一线程内 `filterOpInstance` 返回同一指针；
语义 oracle 仍 diffs=0（算法搬移行为等价）。全套 12/12，主程序干净重建 0/0。

**仍欠（下一轮）**：其余**有资源**的滤镜（中值、锐化 USM 等）新增时逐个迁入 `FilterOp`，不要再写自由函数；
~~「调整层 + 面积算子」的 ROI 外扩~~ ✅ 已接子缓冲路径（见上节与 `adjarea`）。

---

## 滤镜接 UI：方框模糊从菜单可用（第 3 步）

**为什么现在做**：引擎侧（面积算子 + ROI 外扩 + `FilterOp` 三段式）已就位，但「滤镜(&R)」菜单里
~50 个 action 全是**灰显占位**、`BoxBlur` 只能从代码调用 —— 前面几轮的性能与正确性工作对用户完全不可见。
这一步先把「菜单 → 参数对话框 → 非破坏应用到图层」这条闭环打通。

**改法**：

| 环节 | 做法 | 为什么 |
|---|---|---|
| 加滤镜的通用入口 | 新增 `ImageDocument::addLayerFilter(const FilterNode&)`（push undo + 失效栅格 + markDirty，返回下标） | 原先只有 `addBrightnessContrastFilter` 一个硬编码入口，加不了别的算子 |
| 参数面板 | `AdjustmentPropsHost` 增一页「盒模糊：半径 0..128」，该页**程序化建**（无 .ui） | 它已有「按 OpName 切页 + 预览/提交信号」，复用它比新写面板省；程序化建页避免为一个滑块新增 .ui + FORMS |
| 对话框 | 新增 `ui/filterdialog.{h,cpp}`：`QDialog` = 参数面板 + OK/Cancel，只转发 `previewChanged`，**自身无状态、不碰文档** | 「应用/取消」的语义属于文档层，放 `MainWindow`；对话框做成哑组件才能被复用（将来调整类滤镜也能挂） |
| 应用语义 | 节点先以 `pushUndo=false` **临时**加入（供预览）→ 确定时「摘掉临时节点 + 正式加入（`pushUndo=true`）」；取消则摘掉临时节点 | 让**一次滤镜操作 = 一条撤销**：撤销点恰是「加滤镜之前」；取消不留痕 |
| 菜单 | 「滤镜 → 模糊 → 方框模糊…」置 enabled + 连 `runFilterDialog(OpName::BoxBlur)` | 其余分类保持占位，不假装有功能 |

**预览的取舍（已知限制）**：像素层滤镜的预览是**整区重合成**（`setLayerFilterNode(..., false)` → markDirty），
没有「调整层拖参」那种降采样 + 异步 + 单飞的路线。小文档流畅；大文档拖半径会重合成整层。

**验证**：`adjustpanel` 增一条「盒模糊半径 7 → 回填 7」（守住新页的控件命名与回填契约）。
> 这条断言还暴露过一个**测试自身的坑**：`setNode` 会切换「当前页」，把断言插在别的页的断言之间
> 会让后续 `findChild` 落空 —— 现已把盒模糊断言放到用例末尾。

全套 12/12；主程序干净重建 0 warning / 0 error。

**撤销原子性（实测反馈后修正）**：用户反馈「点确定后撤销能撤回；**没点确定按撤销撤不回来**」。
离线复现出两个真实问题：

1. **打开对话框就 push 了一条「添加滤镜」**，取消时又 push 一条「删除滤镜」→ 取消后按 Ctrl+Z
   会把刚取消掉的滤镜**撤回来**（用户感受就是「撤不回来 / 反了」）；确定也要按**两次** Ctrl+Z 才干净。
   改法：`addLayerFilter` / `removeLayerFilter` 增 `pushUndo` 参数，对话框流程用临时节点，
   确定时才正式加入 —— 一次操作恰好一条撤销。
2. **模态对话框会吞掉主窗口的 Ctrl+Z**，拖动参数后按 Ctrl+Z 毫无反应。
   改法：`FilterDialog` 自带一条 Ctrl+Z，**逐步回退参数**（每次参数提交记一份「提交前」的节点，
   回退后发 `previewChanged` 让画布跟着退）。它只动参数、不碰文档撤销栈 —— 文档级撤销仍由确定/取消决定。

**验证**：新增常驻用例 `filterundo` 离线复刻整条流程 —— 确定 → **一次**撤销即 filters=0 且画面与原样逐像素相同；
取消 → 状态原样且不改变撤销栈可用性。全套 13/13；主程序干净重建 0 warning / 0 error。

**仍欠**：① 大文档的滤镜预览仍是同步整区重合成（需异步/降采样，与 Roadmap 9.2 同一条）；
② 对话框还没有「预览」勾选框与前后对比视图；③ `HistoryStack` 未暴露深度，测试只能断言行为、无法断言条目数。

---

## 退出软件崩溃：thread_local 里跨调用保留 QImage（实测事故）

**现象**：滤镜功能与撤销都正常；**点软件右上角退出时崩溃**。GDB 栈：
`run_dtor_list → ~unordered_map → 释放节点`（`tls_callback`）—— 即 TLS 析构里在销毁一个 map。

**根因**：上一轮把「每线程一份的滤镜算子实例」放进 `thread_local std::unordered_map`
（`engine/op/filterop.cpp`），而 `BoxBlurFilterOp` 为复用临时缓冲**跨调用持有两块 `QImage`**。
**TLS 析构可能在 Qt 静态数据之后运行** → 那时销毁 `QImage` 会踩到已失效的 Qt 状态。
全项目**只有这一处** `thread_local` 持有 Qt 对象（其它 static 表里的算子都不长期持有 `QImage`：
`FloodFillOp` 等在 `finish` 里清空），与栈信息吻合。

**改法**：

| 决定 | 为什么 |
|---|---|
| 实例表改为「`thread_local` **指针** + 堆上容器、**永不 delete**」 | TLS 对象变成 trivially destructible，退出时**根本不跑析构** —— 对该类问题的两种假设（Qt 静态已失效 / CRT 已收场）都免疫，比「退出前显式释放」更稳 |
| 跨调用保留缓冲上限 1 Mpx → **128×128** | 容器不再析构 ⇒ 线程退出时缓冲也不回收，而 Qt 线程池的闲置线程会过期重建 → 上限不压小会累积。128² 仍覆盖最热的 **64×64 块**路径 |

**代价（实测；基准原先漏调 `opsInit`，读到 0.000 ms 的假数据，已修）**：

| 场景 | 池（1 Mpx 上限） | 现在（128² 上限） |
|---|---|---|
| 64×64 r=2（最热，仍命中池） | 0.039 ms | 0.042~0.044 ms（噪声内） |
| 256×256 r=4 | 0.629 ms | 0.71~0.92 ms |
| 1024×1024 r=8 | ~15~18 ms | 20.1~20.7 ms |

**验证限制（如实说明）**：这个崩溃**在 offscreen / 测试环境复现不出来**（同一份代码在测试里退出正常），
所以只能靠「消除 TLS 析构」从根上保证 —— **真实 GUI 的退出需要实跑确认**。
同类问题已写进规则：`.cursor/rules/no-latent-bugs.mdc` §12。

**附带发现**：`FilterEval::applyNode` 依赖 `opsInit` 注册（未注册时面积算子会**静默不处理**）。
这与既有 `OpRunner`/`OpRegistry` 的前提一致（app 的 `main()` 已调用），但验证程序漏调就会读到假数据（本轮踩到）。

**仍欠**：大区域（整层级）现在每次调用都分配两块缓冲（约 15% 开销）。若要兼得，可加
**全局保留字节预算**：预算内保留、超预算即释放 —— 既限住泄漏，又保住大区域的复用。

---

## 新增两个面积算子：USM 锐化 / 查找边缘（滤镜库第 2 批）

**为什么先做这两个**：滤镜菜单刚接通一条完整链路（参数对话框 + 预览 + 非破坏 + 撤销），此时补算子最能
检验 `FilterOp` 族是否好扩展。选它们是因为性质互补：USM 需要**模糊级**（考「算子能否复用算子」），
查找边缘是 **3×3 小 pad**（考 ROI 外扩的边界正确性）。

| 算子 | 对照 | 实现要点 |
|---|---|---|
| `UnsharpMask` | `gegl:unsharp-mask` / PS「USM 锐化」 | `diff = src - blur(src,r)`；`\|diff\| > threshold → out = src + amount*diff`（逐通道、0..255 尺度）。**模糊级直接复用 `BoxBlurFilterOp`**：通过 `filterOpInstance(BoxBlur)` 走**对方自己的**三段式（纯虚接口，不必知道对方类型）—— 少一份可分离卷积实现，也顺带证明 FilterOp 族可组合 |
| `FindEdges` | `gegl:edge-sobel` / PS「查找边缘」 | 标准 3×3 Sobel，幅度 `√(gx²+gy²)` 逐通道夹到 255，alpha 保留。**只需 2 行滚动缓冲**（不做整图拷贝）：逐行写回会破坏下次要用的邻域，故把 y-1 / y 行的**原始直通**像素留在缓冲里，y+1 行直接读 |

**新增算子的完整清单（本轮踩点，供下一个算子照抄）**：`OpName` 加枚举 → `opname.cpp` 补 id/title 与
`isAdjustmentOp` 排除 → `FilterNode` 加参数与 `inputPad()` 分支 → 参数进 `projectio` 固定块（**只在尾部追加**）
→ 写 `FilterOp` 子类 → `opsInit` 注册 → 参数页（USM 三滑块；查找边缘无参数）→ 菜单接线
（`actionUnsharpMask` 开对话框、`actionFindEdges` 直接应用）→ 测试 + 文档。

**abyss 口径**：查找边缘也用「层外即透明」（与 BoxBlur 一致）；**GIMP 的 `edge-sobel` 用 clamp** ——
这是刻意取舍（项目内一致 > 与 GIMP 逐位一致），差异记在此处。

**本轮自捕获的 bug（缺陷台账 #6）**：`FindEdges` 初版直接在**预乘**像素上求导、再把结果写回预乘缓冲
= **二次预乘** → 半透明边缘又暗又钝。写语义 oracle（朴素 3×3 Sobel）时对不上才暴露。已补直通↔预乘两趟。

**验证**（`areafilter` 扩到 3 个算子）：

| 检查 | 结果 |
|---|---|
| USM 分块投影 == 整幅合成（无跨块接缝） | diffs=0 |
| USM 强度 0 == 未加滤镜（`prepare` 跳过 `process` 的语义） | diffs=0 |
| 查找边缘 == 朴素 3×3 Sobel（语义 oracle） | diffs=0 |
| 查找边缘分块 == 整幅（**1px pad 漏了就会在 64×64 块界出现接缝**） | diffs=0 |
| USM 半径/强度/阈值 存读往返 | 全保真 |

全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① 中值模糊（需要直方图/多级中值，是真正的「重资源」算子）；② 位移（可参考 GIMP 自己的
`gimpoperationoffset.c`，其 abyss 策略更完整）；③ 调整层 + 面积算子仍未接；④ 面积算子的参数页还没有
「预览」勾选框与前后对比。

---

## 新增高斯模糊算子（第 3 批）—— 以及**一次被推翻的计划**

**先说被推翻的计划（留痕规则要求）**：我上一条给自己的建议是「先中值、再高斯」。
动手前评估发现：中值是 O(r²)/像素量级，而当前预览是**同步整区重合成**（见「仍欠」那条）——
在演示文档 2480×3508 上拖一次半径就是秒级卡顿，等于把已知欠账立刻引爆。
故**改成先做高斯**（快、复用已验证的盒模糊），中值排到「预览护栏」之后再做。

**实现**：`GaussianBlurFilterOp` —— 用**三次盒模糊**逼近高斯（GIMP/经典做法）。

| 决定 | 为什么 |
|---|---|
| 三次盒模糊，而不是直接卷积高斯核 | 可分离高斯核宽 = 6σ+1，逐像素成本随 σ **线性增长**；三次盒模糊每趟是滑动窗口求和，**O(1)/像素、与 σ 无关**，且能直接复用已优化的 `BoxBlurFilterOp` |
| 本算子**自己不持有缓冲** | 三趟的临时缓冲由盒模糊算子自己的实例管 —— 顺带说明 `FilterOp` 的 `prepare/finish` 对「无资源算子」是可以不实现的 |
| UI 参数叫「半径」，σ = 半径/3 | PS 未公开其半径↔σ 的精确映射，所以把口径写死，并用**量化误差**替代「和 PS 一样」的含糊说法 |
| `inputPad() = 半径 + 2` | 三趟盒半径之和 ≈ 0.98×半径，留 2px 余量。**宁可比需要的外扩多一点**：外扩多了只是多算，外扩不足会在 64×64 块界出现接缝 |

**精度（不是「差不多」，是数字）**：测试里用**朴素精确高斯**（可分离、直方核、同 σ 同 abyss）做参照，
只比内部区域（边缘受 abyss 影响、不算逼近误差）：
**内部最大单通道误差 = 3/255**（断言 ≤8/255）。

**又一次「先量后改」推翻了注释里的判断**：我在 USM 的头注释原写「二次往返不可逆、会掉色」。
推导后发现不成立 —— `qPremultiply(c,a) = c·a/255 ≤ a`（c ≤ 255），**永远不会被我原先担心的那个 clamp 咬到**；
直通↔预乘往返的损失只有 ±1 LSB。故高斯走三趟盒模糊（= 3 次往返）也是安全的，
注释已改成准确说法（多一次往返 = 多一趟全图读改写 + ±1 LSB，故能省则省）。

**验证**：`areafilter` 扩到 4 个算子 —— 高斯分块 == 整幅（diffs=0，**证明 pad 上界够**）、
精度量化 3/255、往返保住半径 8。全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① **同步整区重合成**（现在有 4 个面积算子，这个大文档预览卡顿的欠账越来越贵 —— 建议下一步就做它：
降采样预览 + 可取消）；② 中值模糊（需要滑窗直方图 + 上述预览护栏）；③ 位移；④ σ 与 PS 的对齐只能靠肉眼，不可宣称「一致」。

---

## 滤镜预览的两道护栏：只刷新可见区 + 节流（第 4 批）

**为什么做这个**：面积算子已有 4 个，而预览一直是「每个 slider tick → 整层标脏 → 整层重合成」。
文档越大越痛；像中值这种 O(r²)/像素的算子一来，拖一次半径就是秒级卡死 —— 等于把「仍欠」里的欠账引爆。
所以**先补护栏，再上重算子**。

| 护栏 | 做法 | 为什么 |
|---|---|---|
| 只刷新**可见区** | 新增 `ImageDocument::setLayerFilterNodePreview(layer, filter, node, region)`：写节点、失效层栅格、**只标脏 region**；`CanvasView::visibleDocumentRect()` 给出视口对应的文档矩形（从 `updateProjectionPriorityRect` 里提出来**共用同一处数学**，避免两处漂移） | 滤镜对话框是**模态**的 → 预览期间视口不会变，屏幕外的旧像素看不见，所以「只算看得见的」在预览阶段是安全的；定稿走 `addLayerFilter`/`removeLayerFilter`，会整层标脏，屏幕外最终仍会刷新 |
| **节流 80ms**、只带最新值 | `previewThrottle`（`singleShot` + `if (!isActive()) start()`）：拖动中每 80ms 落一次，且永远用**最新**节点 | 原先是「每个 tick 都提交一次重合成」→ 快速拖动会把任务**排队堆起来**（越拖越落后）。节流保证最多 ~12 次/秒且不积压 |

**验证**（`filterundo` 新增场景 C，在**投影层面**断言）：

| 检查 | 结果 |
|---|---|
| 区域预览后**区内**已刷新 | diffs=4066 |
| 区域预览后**区外**保持旧像素、未重算 | diffs=0（256×256 = 16 个 64×64 块里只重算了 1 块） |

> 这条断言也让我踩了一次测试自身的坑：投影是在 `sync()` 里**取脏区**的，预览后不调 `sync()` 就什么都不重算
> —— 表现为「区内也没刷新」，差点误判成产品 bug。

全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① 打开对话框时那一次仍是整层标脏（一次性成本，未区域化）；
② 预览**不可取消/非异步**：单块的重滤镜（例如将来的大半径中值）仍会阻塞到算完 —— **这是中值之前真正要补的下一块**；
③ 没有降采样预览（PS 那种「更糊但更快」的档）。

---

## 新增中值模糊算子（第 5 批）—— 滑窗直方图 + 首个「重滤镜」

**为什么用滑窗直方图**：朴素做法每像素「取窗口 → 排序 → 取中」（r=3 是 49 个样本 ×4 通道的排序）。
滑窗直方图把「窗口平移一格」的代价压到 **2(2r+1) 次计数增减**（r=3：14 次/通道），**与窗口面积无关** ——
这是中值能从「不能用」变成「能用」的关键。

**资源形态的对照（刻意的）**：本算子唯一的常驻资源是 **4×256 的窗口直方图（恒 2 KB，与 ROI 尺寸无关）**，
所以它**不需要** `BoxBlurFilterOp` 那套「超过上限就放掉」的逻辑（`finish` 里只复位参数）。
`FilterOp` 的三段式因此同时容纳了两种资源形态：**区域尺寸缓冲**（盒模糊/USM）与 **O(1) 小状态**（中值）。

**就地写回的坑（实现要点）**：中值要读窗口行 y-r..y+r，而输出又是就地写回 image ——
所以**不能**回头读 image（它正在被写）。做法是只缓存窗口需要的 **2r+1 行原始（直通）像素**（内存 O(r·w)，
不是整图），每行在**被覆盖之前**恰好装载一次；取样本一律走这个环形缓冲。

**abyss 口径**：越界样本按**透明（0）**计入直方图，窗口样本数恒为 (2r+1)²、不做重归一化（与 BoxBlur 一致）。

**实现范围（子集，不假装齐全）**：只做**方形邻域 + 50% 分位**；`gegl:median-blur` 的
percentile / circle / diamond 邻域**没做**。

**成本（实测，不是估计）**：`512×512 r=3 → 145 ms ≈ 553 ns/像素`（本机单次噪声可达 ±50%，只取量级）。
推算：可见区（约 1200×800）一次预览 ≈ 0.5 s；演示文档整层（8.7 Mpx）定稿 ≈ 5 s 量级。
但投影是**按 64×64 块 + 时间预算**推进的，所以 UI 不会冻死 —— 表现为「可见区逐步变糊」。
**目前占大头的是找中值那 256 个 bin 的直接扫描**（滑窗更新只 4×(2r+1)≈28 次/像素，扫描最多 4×256 次）。
已知下一步：用**增量 cum** 维护中值位置，把定位降到摊还 O(1) —— **当时尚未实现、也尚未实测，故未写倍数**。
> ✅ **已落地**（见后面「中值：增量 cum 定位（第 9 批）」）：实测 2.80×（r=3）/ 4.44×（r=1）/ 1.93×（r=8），逐像素等价。

**验证**（`areafilter` 扩到 5 个算子）：

| 检查 | 结果 |
|---|---|
| 中值 == 朴素**排序**中值（语义 oracle，刻意用与实现不同的方法） | diffs=0 |
| 像素层中值：分块投影 == 整幅合成（pad=r 够，无跨块接缝） | diffs=0 |
| 中值半径存读往返 | 半径 3 保住 |

全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① ~~找中值的增量 cum 优化~~（**已完成**，见「中值：增量 cum 定位（第 9 批）」）；② percentile/圆形/菱形邻域；
③ 预览仍**不可取消**（重滤镜下单块会阻塞到算完）；④ 中值的**定稿**（整层）仍是秒级，需要异步 + 进度/取消。

---

## 中值：增量 cum 定位（第 9 批）—— 一个 `O(256)/像素` 的常数项，与它带来的三个 bug

**为什么先做它**：这是 pending-dev 里少数**已经有数据支撑**的下一步（上一节实测标出「占大头的是找中值那 256 个 bin 的直接扫描」）。
它不是"再优化一点"，而是把一个**与半径无关的固定成本**（每像素 × 每通道扫 256 bin）换成与半径同阶的成本。

### 改法

滑一格窗口只让样本「进 2r+1 个 / 出 2r+1 个」，所以不必每像素重扫：

| | 旧（直接扫描） | 新（增量 cum） |
|---|---|---|
| 维护的量 | 直方图 | 直方图 + **每通道** `p`（当前中值 bin）与 `cum`（比 `p` 小的样本数） |
| 每像素定位 | 从 bin 0 累加到 ≥ 第 half 小（≤256 次访存 ×4 通道） | 进出样本各修正 `cum`（±1），再把 `p` 移回不变量 `cum < half ≤ cum + hist[p]` 成立处（**≤ 2r+1 步**） |
| 不变量复位 | —— | 每行开头重建直方图时顺手 O(256) 复位一次（每行一次，可忽略） |

实现见 `engine/op/medianblurfilterop.cpp`；`locateMedianByScan()`（同一直方图上的朴素扫描）**只给测试**做交叉验证。

### 受控交替 A/B（同进程、同一二进制、交替跑「旧实现副本」与「新实现」，各取多轮最小值）

| 场景 | 旧 | 新 | 提速 |
|---|---|---|---|
| 512×512 r=3 | 136.5 ms（520.8 ns/px） | **48.7 ms（185.8 ns/px）** | **2.80×** |
| 512×512 r=1 | 99.4 ms（379.3 ns/px） | **22.4 ms（85.4 ns/px）** | **4.44×** |
| 256×256 r=8 | 57.6 ms（879.4 ns/px） | **29.8 ms（454.6 ns/px）** | **1.93×** |
| 200×150 r=2 | 12.2 ms（407.9 ns/px） | **3.9 ms（130.7 ns/px）** | **3.12×** |

（同一数字在常驻用例 `areafilter` 内的 512² r=3 实测为 **48.7~51 ms / ~186~195 ns·px⁻¹**，与上表一致；
本机单次运行噪声可达 ±50%，故只取量级与倍数结论。）

**等价性**：同一交替程序内「旧 vs 新」**逐像素不一致 0 处**（4 组场景全 0）；
另由常驻用例 `areafilter` 的**朴素排序中值 oracle** 覆盖 **r=1..4** 逐像素钉住。

> ⚠️ 数字来自一次性验证程序（`.build-verify/medianab.cpp`，用完即删、不入库），
> 旧实现是按语义**逐字复制**进去的（刻意不引用仓库源码，避免"两边一起改错"）。
> 提速曲线与「成本 ∝ 半径」的预期一致：半径越大，滑窗更新（本就 O(r)）越占主导，加速比越小。

### 这一轮真实踩到的三个 bug（成品比原实现多踩了这么多，故全部留痕）

改造把「每像素重建答案」换成「跨像素维护状态」，于是**旧实现里无害的写法全部变成致命**：

| # | 现象 | 根因 | 改法 / 防复发 |
|---|---|---|---|
| 台账 8 | 中值落在 **bin 0** 的像素起成片变黑 | 回退循环无下界：`cum = 0 < half ≤ cum + hist[0]` 是**合法**的终止状态，但代码继续 `--p` → `p = -1` → 越界写 `m_hist`（且此后状态全坏） | `while (p > 0 && …)`；`Q_ASSERT(p >= 0 …)`；oracle 扩到 r=1..4（该 bug 只在「窗口内含大量透明/越界样本」时触发，正是 r≥2 的图像边缘） |
| 台账 9 | 直方图和 **≡ 25 (mod 2¹⁶)** 却内容全错（65561/25） | 同一行有**两条取槽路径**：`samp()` 用未夹紧的 `y` 取模（`-2 % 3 == -2`），直查用夹紧行号；且 `QVector<QVector<QRgb>> ring(n, QVector<QRgb>(w))` 的**重复值构造让各行共享缓冲**（`constData()` 全部相同） | 只留**一条** `rowAt(row)`（越界即 nullptr）；ring 改**逐行 append**。**教训：环形/重复值容器要么逐行建，要么把「行号 → 槽号」收成一个函数——两处各算一次必然漂** |
| 台账 10 | 直方图每像素和 35/25 并持续增长 | 越界样本被写成「只加不减」（`qDel = delIn ? … : 0`，越界时少减一次 `hist[0]`） | 进/出样本**各 (2r+1) 个**，越界一律按 0 **照常计数**（与建立直方图同一口径）。**教训：滑窗更新的不变量是「样本数守恒」，口径不一致会立刻破坏它** |

> 定位手法也留痕：先用「同一张直方图上的**直接扫描**」跟增量结果比，**一次**就分清了
> 「错的是直方图内容」还是「错的是增量定位」；再用「ring 槽 vs 原图行」逐行对比抓到槽号错。
> 这两个探针都不入库（用完即删），但**同一直方图上的朴素扫描**留成了 `locateMedianByScan()` 供测试复用。

### 仍欠

① percentile / 圆形 / 菱形邻域仍未做；② ~~预览/定稿仍**不可取消**~~ —— **定稿已落地**（见「滤镜定稿的异步/可取消（第 10 批）」）；
预览（拖参数那一侧）仍走 `setLayerFilterNodePreview` 的区域重算 + 80ms 节流，未接同一套后台任务；
③ 中值定稿（整层）已从 UI 线程移走，但总墙钟仍是秒级量级（未做可见区增量）。

---

## 新增锐化算子（第 6 批）+ **一个必须先补的架构缺口：ROI 只能表达「对称外扩」**

**计划为什么改（留痕）**：原计划这一轮做**位移 / offset**（还想顺带把 abyss 参数化）。按项目规则先去读
GIMP 的参考实现 `app/operations/gimpoperationoffset.c` —— 结果发现**现在的机制装不下 offset**，故改做能装进去的 3×3 锐化，
并把缺口记在下面。这不是"换个简单的做"，而是先把架构结论钉住。

### 缺口：`inputPad()` 只能外扩，不能平移

| | GIMP / GEGL | 本项目现状 |
|---|---|---|
| 需要输入的描述 | `get_required_for_output(op, input_pad, output_roi)` 返回一个**通用 `GeglRectangle`** —— offset 就是「output_roi 平移 (dx,dy)」（`gimp_operation_offset_get_rect(..., TRUE, ...)`） | `FilterNode::inputPad()` 返回一个**标量**，调用方做 `want.adjusted(-p,-p,p,p)` |

后果：
- **offset 的正确答案是「平移后的同尺寸矩形」**，用对称外扩去逼近就得 `p = max(|dx|,|dy|)`，
  于是每个 64×64 块要取 `(64+2p)²` 的输入 —— 偏移 500px 时是 **~270 倍**的输入量，正确但荒唐。
- **wrap-around（PS 默认、GIMP 的 `GIMP_OFFSET_WRAP`）更装不下**：它是**多矩形**依赖（区域在右边缘时要读最左边），
  单矩形机制无法表达；退化成「整层输入」则变成 O(块数 × 整层) ✗。

要补的东西（**尚未做**，下一步的架构项）：`FilterContext` 带 **src/out 两个矩形**（而不是让算子自己按 pad 猜中心裁剪）、
`inputPad()` 升级为 `requiredInputRect(want)`、五个面积算子的裁剪与内环几何改为读 context；
wrap 还需要把 graph 的 `requiredInputs` 从「一个矩形」升级为「多矩形」。

### 本轮实际做的：3×3 锐化

| 决定 | 为什么 |
|---|---|
| 3×3 线性核，强度 s：中心 `1+4s`、四邻 `-s`（核权和恒为 1） | 权和为 1 ⇒ 保持均值、只增强高频，边界不会整体漂移；s=1 即经典锐化（中心 5 / 四邻 -1） |
| 只缓存 2 行原始像素做滚动窗口（O(w)），不做整图拷贝 | 与 `FindEdges` 同构：逐行写回会破坏下一次要用的邻域 |
| 与 USM 锐化并存，不合并 | USM 有半径 + 阈值（可控、能抑噪）；3×3 是固定邻域的高频增强（快得多）。PS 里两者也并存 |
| 接线到「滤镜→锐化→**进一步锐化**」 | 该菜单项在 PS 中就是更强的 3×3 锐化；强度 1 覆盖普通「锐化」 |

**验证**（`areafilter` 扩到 6 个算子）：

| 检查 | 结果 |
|---|---|
| 锐化 s=1 / s=2 == 朴素 3×3 卷积（语义 oracle） | diffs=0 / diffs=0 |
| 强度 1 vs 3 结果不同 | 有差异 |
| 像素层锐化：分块投影 == 整幅合成（pad=1 够，无跨块接缝） | diffs=0 |
| 强度存读往返 | 保住 2 |

全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① **ROI 通用化**（上面那条缺口，是 offset / 任何几何类算子的前置）；② 浮雕（`actionEmboss` 已存在，同属 3×3 家族，随时可加）；
③ ~~中值找中值的增量 cum 优化~~（**已完成**，见「中值：增量 cum 定位（第 9 批）」）；④ ~~异步/可取消的定稿~~（**已落地**，见第 10 批）。

---

## 滤镜定稿的异步/可取消（第 10 批）—— **已落地**

**为什么先做它**：这是本文件里排在最前面、也最贵的一条欠账（「面积算子已有 7 个，预览虽有护栏，
但**定稿**仍是同步整层」）。它不是「体验优化」，而是**重滤镜能不能用**的分界线：

- 定稿原本发生在**合成路径里**：整层标脏 → `Projection::sync` 逐块合成 → 每块现场跑滤镜链；
- 单块代价 = `(64 + 2·pad)²` 的滤镜求值。中值 512² r=3 ≈49 ms，折算到演示文档
  （2480×3508 ≈ 8.7 Mpx）**整层就是秒级**，表现为「画布一块一块慢慢糊」且无法取消。

### 做法：快照 → 后台条带并行 → 结果按矩形交给层缓存

| 环节 | 决定 | 为什么 |
|---|---|---|
| 线程安全 | 工作线程**只读**一张主线程抽好的 `QImage` 快照（请求区 + pad），**绝不**碰 `ImageDocument`/`Layer`/`TileBuffer` | 主线程随时可能写瓦片；跨线程读稀疏瓦片是数据竞争。快照成本是一次区域拷贝，换来的是**不需要加锁** |
| 后台并行 | 按**横向条带**切 `QtConcurrent::blockingMap`，每条带取「输出行 ± pad」的输入、求值后裁回 | 整层求值原本在合成路径里就是并行的；改成单线程反而更慢（实测 1600×1200 中值 r=3：并行分块 112 ms vs 单线程整层 355 ms）——那就成了「不冻了但更慢」。pad 是硬约束：条带太薄时 pad 会把相邻条带的输入吃光，故按 pad 反推条数上限 |
| 结果交接 | 回主线程把结果写进 `Layer::setRegionFilterResult(rect, img)`；`compositeRasterRegion` 命中后只做「取子图 + 混合」 | 合成路径是同步的（`Projection::sync`），但命中缓存后它不再跑滤镜；于是**渐进式分块**的观感保留、每块却很便宜 |
| 取消（协作） | `FilterStack::apply(source, shouldAbort)` 把谓词注入图里每个节点；7 个面积算子在行循环里周期性查询（每 32 行 / 中值每行） | 取消要能**中途**停手，否则线程池会一直算一份没人要的结果 |
| 取消（作废） | `Layer::contentGeneration()`：任何层内容改动都 ++（汇聚在 `invalidateCompositeRaster`）；回调比对世代号，不一致就丢弃 | 比「依赖信号到达顺序去 cancel」可靠得多：只要中间发生过任何改动，过期结果**一定**写不进去 |
| 世代号接替 | `LayerFilterJob` 每次 `start()` 自增 `runId`，回调只认自己那一代 | 同一改动会连续触发两次 `contentChanged`；若第二次拒启就成了「第一个被取消、第二个没起来」→ 一次都不写（实测） |
| 进度 | 后台原子进度 + 画布 100ms 轮询 → 底栏状态条一行「滤镜定稿：x%」 | 进度不该跨线程发信号；轮询是最省事又不失真的做法 |
| 触发判据 | 只在「活动层有**启用且 `inputPad() > 0`** 的滤镜」时起后台任务；滤镜栈指纹未变则不重复起 | 纯点滤镜逐像素、整层也就几毫秒，异步化只是徒增复杂度；层移动/显隐等无关 `contentChanged` 不该重启任务 |
| 不适用 | 调整层、有启用样式的层不参与（它们不走区域缓存） | 调整层滤的是下方合成结果、样式需外扩 halo 走整层缓存，各有自己的通道 |

### 受控 A/B（一次性程序，同机同进程）

| 场景 | A 同步定稿（UI 线程，每块现场跑滤镜） | B 后台单线程整层 | B′ 后台条带并行 | B 缓存就位后的分块合成（UI 线程） |
|---|---|---|---|---|
| 1600×1200 中值 r=3 | **112 ms**（全部在 UI 线程） | 355 ms | **58 ms**（UI 不冻） | **12 ms** |
| 800×600 中值 r=3 | **43 ms**（全部在 UI 线程） | 103 ms | **23 ms**（UI 不冻） | **4 ms** |

**等价性**：A vs B、B′ vs 整层，**逐像素不一致 0 处**（两组场景全 0）。

> 读法：改动后**UI 线程**只剩「分块合成」那 12 ms（且是分帧的），原来要占 UI 线程的 112 ms
> 挪到了工作线程；总墙钟时间（58 + 12）比原来（112）略长，换来的是**窗口全程可交互、可取消**。

### 这一轮踩到的三个真 bug（端到端才暴露，全部留痕）

| 台账 | 现象 | 根因 | 改法 |
|---|---|---|---|
| 13 | 回调写缓存成功（`covers=1`）后**紧跟一条失效**，端到端永远等不到缓存 | 先写缓存、后 `markDirty()`；而 `ImageDocument::markDirty` 会 `invalidateCompositeRaster()`（连带清区域缓存）→ 自己清自己 | 顺序改为**先 markDirty 再写缓存**，再补发一次 `contentChanged` |
| 14 | 一次都不写缓存 | 同一改动连续触发两次；第二次 `start()` 因「上一个还在跑」返回 false，却**已经取消了上一个** | `start()` 改**接替**语义（`runId` 世代号），不再拒启 |
| 15 | 条带并行结果与整层差 24 万像素 | 多条带用 `QPainter` 并发写同一张 `QImage` | 逐行 `memcpy`（与分块并行合成同一修法） |

> 教训（比结论值钱）：**「写缓存」与「标脏」这类看似无关的两步，在同一个函数里顺序是有语义的**
> —— `markDirty` 在本项目里兼有「失效活动层缓存」的副作用，排在写缓存之后就会静默抹掉结果。
> 断言守不住它（两处单独都正确），只有端到端断言能。

### 验证

新增常驻用例 `filterasync`（14 条断言）：协作式取消真的中断面积算子（谓词被调用、结果是半成品）；
缓存命中返回的像素来自缓存且尺寸/坐标正确；失效后世代号前进、缓存不再命中；无缓存（同步）结果
== 直接 `FilterStack::apply`；**端到端**（离屏 `CanvasView` 触发 → 后台任务 → 缓存就位 →
命中缓存的投影 == 整幅合成，diffs=0）。全套 **15/15**；主程序干净全量重建 0 warning / 0 error。

### 仍欠

① 进度只有「已开始 / 已结束」两档（`apply` 内部没有逐节点回调），底栏百分比其实跳变；
② 后台求值仍要**整层**跑一遍，未做「只算可见区、滚动画布再补」的增量；
③ 有启用**样式**的层与调整层不参与本条链路（各有各的缓存通道）；
④ ROI 通用化（下一节）仍未做 —— 它是 offset 之类几何算子的前置。

---

### 后续：环绕已**彻底移除**（用户决定）

用户明确「不需要保留环绕」，于是把它删干净，而不是留成选项：

| 删除项 | 说明 |
|---|---|
| `OffsetFilterOp` 的 wrap 分支 | 只剩非环绕一条路（移出即丢、空出透明） |
| `OffsetFilterOp::kNeedsWholeImage` | 位再也没有「必须整幅求值」的算子 |
| `FilterNode::offsetWrap()/setOffsetWrap()` | 连同成员字段 |
| `FilterStack::hasGeometryOp()` | **整个方法删掉** —— 它唯一的使用者就是环绕位移。现在 `Layer::compositeRasterRegion` 的整层分支只剩「有样式」与「位置相关算子」两种成因 |
| UI 的「环绕」勾选框 | 位移参数页只剩两个滑块 |

**这一删把整条「多矩形」议题直接消掉了**：环绕是代码里**唯一**的多矩形依赖，
它一走，「要不要支持多矩形 / 第 ③ 步怎么做」就不再是待决问题（见 [engine/roi.md](engine/roi.md) §5）。

**一处刻意保留**：`.pslite` 参数块里那个「环绕位」字节**照写照读但恒为 0**。
参数块是固定长度的追加式布局，中间抽掉任何字段都会让旧文件的后续字段整体错位
（`kFilterNodeParamBytes` 的约定）。所以它降级成「遗留占位」。

**验证**：`offset` 用例重写为 13 条断言（语义 oracle 六组、`requiredInputs` 形状、
分块投影 == 整幅、**位移+中值同栈**也不能块界错位、往返）。全套 **24/24**，App 0 warning。

---

## 位移默认改成「非环绕」—— 一个默认值，两处收益（第 20 批）

**起因**：用户问「一定要环绕位移吗？PS 也是环绕吗？」。查证后发现**我们的默认值抄错了对象**：

| | 默认 | 来源 |
|---|---|---|
| Photoshop「位移」的「未定义区域」 | **设置为背景**（另有「重复边缘像素」「折回」） | [Ps 滤镜：位移](https://blog.csdn.net/qq_41176800/article/details/138642945)（Adobe 官方页 404，未拿到原文） |
| GIMP「Offset」的 Edge Behavior | **Wrap around**（另有背景色 / 透明） | [GIMP 7.49 Offset](https://docs.gimp.org/2.8/zh_CN/gimp-layer-offset.html) |
| 我们（改前） | **环绕** | 当初写注释「对照 gimp:offset」时连默认值一起抄了 GIMP |

**关键**：这不只是「像不像 PS」的问题 —— **非环绕恰好也是能被逐块求值的那一个**。

| | 需要的输入区 | 形状 | 能逐块算吗 |
|---|---|---|---|
| 非环绕 | 输出区 **∪** 平移后的输出区 | **一个矩形** | ✅ |
| 环绕 | 输出区平移后取模（最左边要读最右边） | **两个矩形** | ❌ 只能整层 |

所以把默认改成非环绕是**一举两得**：更贴 PS，且常见情况从「整层算一次」变成「只算请求区」。

### 改了什么

| 位置 | 改动 |
|---|---|
| `FilterNode::m_offsetWrap` | 默认 `true` → **`false`**（= 层外透明） |
| `FilterNode::requiredInputs` | 新增位移分支：非环绕 → `output.united(output.translated(-dx,-dy))`；环绕 → 返回 `output`（走整层，用不到） |
| `FilterStack::hasGeometryOp` | 判据加 `&& n.offsetWrap()`：**只有环绕**才「必须整幅求值」 |
| `FilterStack::containsOffset`（新） | 「含任何位移」——条带并行对它不适用 |
| `LayerFilterJob::start` | 含位移的栈**直接拒绝**，退回同步区域路径（位移是**平移型**需求，条带边界公式 `band ± pad` 对它不成立 —— 正是台账 #20 那类坑，不冒险） |

**位移算子本身一行未改**：它照旧做 `(dx,dy)` 平移，调用方按 `want − srcWant` 裁回，
两步合起来正好等于「从平移后的位置取像素」；越界部分由 extract 填透明 = 层外透明。

### 受控实测（同一进程内 A/B）

| 文档 | 可见区 | 旧：整层 | 新：仅需求区 | 提速 |
|---|---|---|---|---|
| 2048×2048（4.2 Mpx） | 512×512 | 18 ms | **1 ms**（524×520） | **18×** |
| 4096×4096（16.8 Mpx） | 800×600 | 74 ms | **2 ms**（812×608） | **37×** |

### 验证

`offset` 用例扩到 18 条断言：两档语义 oracle 都保留；**非环绕不再被判为「必须整幅求值」**、
但仍被认作「含位移」；**两档各测一次「分块投影 == 整幅合成」**（逐块路径最容易在块界错位）；
直接断言 `requiredInputs` 的形状（单矩形 74×70）；默认值 = 不环绕；往返保住 dx/dy/wrap。
全套 **24/24**，全量连跑 10 次 0 崩溃，主程序 0 warning / 0 error。

### 仍欠

① PS 的第三个选项「**重复边缘像素**」仍未做（实现 = 越界读取钳到边缘）；
② 环绕档仍是「整层算一次」——要变便宜就得先做**多矩形**（见 [engine/roi.md](engine/roi.md) §5）；
③ 位移的 UI 目前还是一个复选框（默认不勾 = 透明）。若要完全对标 PS，应做成三选一的下拉。

---

## 位置相关算子异步化 + ROI 通用化第 ② 步（第 18/19 批）

两件事一起做，因为它们落在同一段几何代码上（`compositeRasterRegion` / `LayerFilterJob`）。

### 第 18 批：位置相关算子（添加杂色）的预览也进后台

第 17 批把「添加杂色」做成位置相关算子后，它是**唯一没有被异步化**的一类：必须整层求值，
而整层路径走 `ensureCompositeRaster`（**不受投影时间预算约束**），8.7 Mpx 高斯彩噪实测
**206 ms 一次**、且是同步的。本次补上：

| 环节 | 决定 | 为什么 |
|---|---|---|
| 判据 | `heavy = pad > 0 \|\| hasPositionalOp()` | 老判据只看 pad，而位置相关算子 pad 恰好是 0 |
| want | 位置相关算子用**整层**（不是可见区） | 噪声按输入图本地坐标取，只有整层一次求值，本地坐标才等于层内坐标 |
| 降采样 | 位置相关算子**禁用**降采样 | 降采样会把坐标一起缩掉，噪声图案就变了 |
| 条带 | 任务里 `strips = 1` | 条带会把 snap 裁成子图，每块 (0,0) 不同 → 噪声在条带界错位 |
| 缓存复用 | `compositeRasterRegion` 在**区域缓存覆盖整层**时直接复用它 | 否则会同步整层重算一遍 —— 等于把刚挪到后台的活又在 UI 线程上做一次（与台账 #13 同类） |
| 不深拷贝 | 缓存正好等于整层时**隐式共享**（不 `copy`） | 8.7 Mpx 的一次 `QImage::copy` 实测 ~14 ms，而投影逐块请求都会走这里 |

**实测（2480×3508 = 8.7 Mpx，高斯彩噪）**：UI 线程阻塞 **206 ms → 22 ms**（整层快照），
命中缓存合成 **0 ms**（隐式共享后）；后台墙钟 296 ms，期间 UI 可交互。

### 第 19 批：ROI 通用化第 ② 步（裁剪/取样按几何，且**能表达不对称**）

`inputPad()`（一个 int）只能表达**对称**外扩。运动模糊 0° 其实只要左右邻居，对称外扩却把
上下也各扩了 `distance` —— 距离 128、块 64 时那等于多取 **4.6 倍**像素。

| 环节 | 决定 | 为什么 |
|---|---|---|
| 需求来源 | 新增 `FilterNode::requiredInputs(output)`（层内坐标，返回矩形），默认实现 = 对称外扩 `inputPad()` | 默认实现与旧行为**逐像素一致**，七个面积算子观感不变（这是第 ② 步的验收标准） |
| 栈级聚合 | 新增 `FilterStack::requiredInputRect(output)`，**自栈顶往栈底倒推** | 栈按顺序作用：最后一个算子的输入需求 = 倒数第二个的输出需求。对称 pad 时倒推与求和等价（故等于旧 `totalPad()`）；**不对称时必须倒推**，求和会把两轴需求混着算多 |
| 取样 | `compositeRasterRegion` 与 `LayerFilterJob` 都改用 `requiredInputRect` | 调用方不再「按对称 pad 猜」 |
| 裁剪 | 仍按几何 `want.topLeft() - srcWant.topLeft()` | 不再假设 pad 对称 |
| 条带边界 | 改成**按侧**的 `padLeft/Top/Right/Bottom`（由**未裁剪**的需求得出） | 层边缘会把 srcLocal 裁掉，但条带仍需向外多取（靠 `∩ snap.rect()` 兜住）—— 用裁剪后的值会让层边缘那一圈算错 |
| 首个消费者 | 运动模糊 0°/45°/90° 声明各自真实需求 | 把「机制」变成「收益」 |

**实测（1024×1024，按 64×64 块逐块「裁输入→求值→裁回」，完全模仿投影）**：

| 场景 | 旧（对称 pad） | 新（链自声明） | 取样面积 |
|---|---|---|---|
| 运动模糊 0°，距离 64 | 160 ms | **39 ms（0.24×）** | 0.35× |
| 运动模糊 90°，距离 64 | 195 ms | **61 ms（0.31×）** | 0.35× |
| 运动模糊 0°，距离 128 | 418 ms | **59 ms（0.14×）** | 0.22× |
| 运动模糊 45°，距离 64 | 256 ms | 219 ms（0.86×） | 1.00×（两向本就需要） |

**第 ③ 步仍然不做，但理由更新了**（原理由「与 offset 绑定后置」已随 offset 走专用路径而失效）：
- ③ = 算子契约改成 `process(src, out, ctx)`、算子自己写输出。**收益**是算子不再计算随后被裁掉的
  边（实测量级 ≈ 1.2–1.25×），以及为将来的**多算子栈**铺路；
- **代价**是七处内环取样基准全改（破坏性），而真正的拦路虎仍是**多矩形依赖**
  （PS 默认位移是 wrap-around：右边缘要读最左边，单矩形机制根本表达不了）；
- 所以顺序应当是「**先决定要不要多矩形**」→ 再决定 ③ 怎么做（或者干脆像 offset 一样，
  继续用整层专用路径）。在拿到「几何算子要进栈中间」的真实需求之前，③ 是纯技术债。
  详见 [engine/roi.md](engine/roi.md) §5。

### 验证

- 新增 `posasync`（位置相关算子异步化：want 放大成整层、任务写整层缓存、域层复用）；
- 新增 `jobteardown`（析构安全，见台账 #21）；
- `hpblur` 扩到 **0°/90°/45° 各测「分块投影 == 整幅合成」**，并直接断言三档的
  `requiredInputs` 只扩该扩的方向（不对称 pad 最容易在条带边界出错，这条是刚需护栏）；
- 全套 **24/24**，全量连跑 **32 次 0 崩溃**（修复前约 1/7 崩），主程序干净重建 0 warning。

### 仍欠

① 第 ③ 步（见上，等「多矩形」的决策）；
② `get 更多位置相关算子`：目前只有添加杂色，且它只能整层求值 —— 若将来给它「输入图在层内的
   原点」（把几何往下传一层），它就能恢复逐块求值；
③ 位置相关算子的**定稿**（点确定）仍是同步整层一次（预览已异步）；一次性的 200 ms 可接受，
   但没测过更大文档。

---

## 添加杂色（第 17 批）—— 非破坏滤镜的**确定性**设计题

**背景**：加法很简单（逐像素加噪声），难的是**确定性**。非破坏滤镜会被**反复重算**：
拖参数预览、定稿、撤销/重做、重开 `.pslite`。若用 `rand()`，每次重算图案都不同 ——
预览会闪、定稿与预览对不上、撤销回不到原样。

### 设计

| 环节 | 决定 | 为什么 |
|---|---|---|
| 噪声来源 | `(x, y, seed, 通道)` 的**纯散列函数**（`engine/filtereval.cpp` 的 `noiseHash`），不是随机数发生器 | 同参数永远同图案 → **不必保存噪声位图**，也就能进任意滤镜栈、任意撤销、任意重开 |
| 种子 | `FilterNode::noiseSeed`，默认固定值，**持久化**进 `.pslite` | 同参数可复现；不同节点可用不同种子得到不同图案 |
| 坐标口径 | 噪声用**输入图本地坐标**，因此调用链必须保证「整层一次性求值」 | 若按 64×64 块分别求值，每块 (0,0) 都不同 → 噪声在**块界错位/重复**（与几何算子必须整幅求值是同一类成因） |
| 路由 | 新增 `FilterStack::hasPositionalOp()`；`Layer::compositeRasterRegion` 与「有样式」「有几何算子」并列走整层路径 | 走整层顺带保证「输入图本地坐标 == 层内坐标」→ 噪声**不随求值区域漂移** |
| 为什么单独一个判据 | 不并进 `hasGeometryOp()` | 成因不同（读别处输入 vs 依赖绝对位置），分开命名便于各自演进 |
| 分布 | 均匀 = `2u−1`；高斯 = **三个均匀量之和**（Irwin–Hall，σ=1、范围 ±3） | 见下 |
| 单色 | `mono` = 三通道加同一扰动（灰噪声）；否则各通道独立（彩噪声） | 对照 PS 的「单色」勾选 |
| 边界 | 数量 0 = 恒等；**全透明像素跳过** | 给不可见像素加噪声没有意义，还会把它变成可见的彩色脏点 |

### 受控实测（每次「拖参数」= 同步整层重算）

| 场景 | 耗时 |
|---|---|
| 1600×1200（1.9 Mpx）均匀 + 单色（**默认档**） | 26 ms |
| 1600×1200（1.9 Mpx）高斯 + 彩噪，**Box-Muller 版** | **535 ms** ← 卡顿 |
| 1600×1200（1.9 Mpx）高斯 + 彩噪，**Irwin–Hall 版** | **42 ms** |
| 2480×3508（8.7 Mpx）高斯 + 彩噪 | 200 ms |

**比功能本身值钱的结论**：Box-Muller 每像素要 `qLn + qCos + qSqrt` 三次超越函数调用，
「高斯 + 彩噪」因此比「均匀 + 单色」慢 **20 倍**。换成「三个散列求和」这类纯整数近似后
（观感相当、分布检验不做），535 ms → 42 ms。**预览要的是手感与观感，不是统计检验。**

### 验证

新增用例 `noise`（16 断言）：同参重复求值逐像素相同、换种子图案不同、数量 0 恒等、
全透明像素不受影响、单色三通道同扰动 / 非单色出现彩噪、**位置稳定**（请求区域与请求整层
返回同一张整层图且文档原点一致 —— 这条会在有人把位置相关算子改回逐块求值时立刻变红）、
整层渲染 == 直接对层内容加噪声、纯点滤镜不被误判成位置相关、参数含种子往返保真、高斯同样可复现。
全套 **22/22**；主程序干净重建 0 warning / 0 error。

### 仍欠

① **位置相关算子还没有异步化**：它必须整层求值，而 `ensureCompositeRaster` 走的是整层缓存
（不是逐块的区域滤镜缓存），所以现有的 `LayerFilterJob` 接不上 —— 预览拖参数目前是**同步**整层重算
（默认档 26 ms 无感；8.7 Mpx + 高斯彩噪 200 ms 会有一下顿）。要做的话：让任务的
`want = 整层`、条带数取 1（位置相关算子不能切条带），并让 `ensureCompositeRaster` 在
区域缓存覆盖整层时复用它 —— 与台账 #13 是同一类「缓存写给谁看」的问题，需谨慎；
② 未做 PS 的「数量 400%+」与「单色以外的高斯/均匀混合」；
③ 噪声是逐通道独立散列（不是 3×3 相关），未做「噪声颗粒大小」参数。

---

## 预览降采样（第 16 批）—— 「更糊但更快」，以及**测得它什么时候是白干**

**背景**：第 15 批把预览挪到后台后，UI 线程不再被堵（225 ms → ~4 ms），但**预览延迟**仍在
（后台墙钟 50–140 ms）。PS 的做法是「降采样预览」：算小图、放大显示。

### 做法

| 环节 | 决定 | 为什么 |
|---|---|---|
| 参数缩放 | 新增 `FilterNode::scaledForDownsampledPreview(divisor)`：**空间类**参数（半径/距离/位移）按倍数缩，**非空间**（强度/阈值/角度/自定义核/颜色）不动 | 只缩图不缩半径，效果会强 `divisor` 倍，预览与定稿差得离谱 |
| 边界 | 半径一律 `max(1, ceil(v/divisor))`，但**原本为 0 的保持 0** | 0 会让算子退化成恒等（预览什么都看不到）；而盒模糊半径 0 本来就是「不处理」，不能凭空造出效果 |
| 求值 | 快照缩小 → 用**缩放后的参数**求值 → 裁出 want 对应块 → 放大回原尺寸写缓存 | 求值量按倍数²下降；不切条带（图已小，单线程够，少一处几何换算易错点） |
| 触发 | 只在「可见区 ≥ 512² **且** 半径 ≥ 4」时降；半径 ≥ 8 用 1/4、≥ 4 用 1/2 | 见下「什么时候是白干」 |

### 实测（一次性 A/B，1600×1200 文档 / 可见区 1200×800 / 中值）

| 半径 | 原分辨率 | 1/2 降采样 | 1/4 降采样 |
|---|---|---|---|
| r=3 | 50 ms | **59 ms（更慢！）** | 16 ms |
| r=6 | 138 ms | 92 ms | 25 ms |

**结论（比功能本身值钱）**：`SmoothTransformation` 的缩放开销大致是常数（≈30 ms，取决于输出尺寸），
**轻滤镜 + 1/2 降采样会被这笔开销吃掉**（r=3 反而慢 9 ms）。所以：
- 半径小（< 4）**不降**；
- 半径 4–7 用 1/2（r=6：138→92 ms，1.5×）；
- 半径 ≥ 8 用 1/4（缩小后半径仍 ≥2，等效半径不走样，能拿到数倍收益）。
这就是为什么策略是**按半径反推倍数**、而不是「区域大就无条件降」。

### 验证

新增用例 `previewdown`（10 断言）：参数缩放（半径 6/2→3、0 保持 0、非空间不动、位移截断、
半径 1/2→1 钳到最小、divisor=1 原样）、任务照常写缓存且尺寸正确、降采样结果与原分辨率
**确实不同**且**大体相近**（平滑图上最大单通道差实测 **1/255**）。
全套 **21/21**；主程序干净重建 0 warning / 0 error。

### 仍欠

① 1/4 目前只在半径 ≥ 8 时启用（更激进的策略会让小半径预览明显走样）；
② 缩放用的是 `SmoothTransformation`（开销大）——若换成「缩小快/放大平滑」的混合策略，
   1/2 档可能不再白干（未测）；
③ 预览未做「拖动中一直用降采样、松手立刻出全分辨率」（目前松手提交走的是定稿路径，
   定稿本身已异步、传 divisor=1）。

---

## 滤镜预览也异步/可取消（第 15 批）—— 补完「重滤镜手感」的另一半

**背景**：定稿（点确定/取消）早在第 10 批就挪到后台了，但**拖参数那一侧仍是同步整区重合成**
（`setLayerFilterNodePreview` → 可见区标脏 → 投影在 UI 线程上现场跑滤镜链）。
这是「重滤镜手感」最后一块短板，也是 [dev-plan.md](dev-plan.md) §6 排第一的遗留项。

### 做法：预览走**静默标脏** + 专用信号，与定稿零耦合

| 环节 | 决定 | 为什么 |
|---|---|---|
| 标脏方式 | `setLayerFilterNodePreview` 改用 **`markDirtyQuiet`**（累计脏区但**不发** `contentChanged`），随后发 `layerFilterPreviewChanged(layerIndex, region)` | 发 `contentChanged` 会让画布**当场同步合成**，而那次合成正是要挪走的东西（等于没异步）。新增的 `markDirtyQuiet` 是为此拆出来的：它只做「记脏 + 失效缓存」 |
| 顺序 | **先** `markDirtyQuiet`（推进层内容世代号），**再**发预览信号 | 后台任务必须捕获推进后的世代号，否则回调的世代校验必然失败（沿用第 10 批的世代号机制） |
| 画布分工 | 收到预览信号**直接**起 `LayerFilterJob`（`want = 给定区域`，不是整层） | 复用第 10 批已有的任务：快照 → 后台条带并行 → 写区域缓存 → 自通知 |
| 退路 | 纯点滤镜（`inputPad()==0`）、几何算子、有样式的层、调整层 → 仍走同步 | 点滤镜几毫秒，异步化只是徒增复杂度；其余各有自己的通道 |
| 任务收尾 | 任务回调改为「**静默标脏 → 写缓存 → 一次性发 pixelsChanged+contentChanged**」 | 原先的「`markDirty` 再写缓存」会在 UI 线程上多跑一次同步合成（那时缓存还没写），白干 |

### 受控 A/B（一次性程序，同机同进程）

| 场景 | A 同步预览（UI 线程现场跑滤镜） | B 异步预览（UI 线程只做 快照 + 起任务 + 命中缓存合成） |
|---|---|---|
| 1600×1200 文档，可见区 1200×800，中值 r=3 | **225 ms / 次参数变更** | **1 + 2 + 1 = ~4 ms**（后台墙钟 53 ms，期间可交互） |

**等价性**：预览区域命中缓存的结果 == 「整层滤镜后裁同块」**逐像素一致**（`diffs=0`，`filterpreview` 用例）。

**本轮踩坑（缺陷台账 #20）**：条带并行的取样/裁回公式**隐含假设 `outLocalLeft==outLocalTop==0`** ——
定稿时 `want` = 整层，pad 被 `intersect` 裁剪吸收，公式退化后仍然对；**预览**时 `want` = 子区域，
pad 是真实存在的，漏掉这两项就「取到未外扩的列、裁回少偏一圈」→ 区域整块像素全错。
**测试提交前抓到**（新用例里预览结果与整层裁块比对，`diffs=10800` = 恰好区域面积）。
教训：**「want 是整层」是一个会让公式看起来正确很久的巧合**，reuse 同一段代码时要显式问
「如果 want 只是子区域，这里还对吗」。

### 验证

新增用例 `filterpreview`（6 断言）：预览**发且只发** `layerFilterPreviewChanged`、**不发** `contentChanged`；
带的区域正确；后台任务写进该区域缓存；任务补发 `contentChanged`；命中缓存的区域 == 整层滤镜后裁同块。
全套 **20/20**；主程序干净重建 0 warning / 0 error。

### 仍欠

① 预览仍按「区域」整块重算，未做降采样预览（PS 那种「更糊但更快」）；
② 预览期间**视口若变化**（非模态对话框下确实可能），已标脏的区域可能不含新可见区 ——
目前靠画布持续写「可见区提示」+ 下一次参数变更补上，未做滚动画布即重算；
③ 调整层预览是另一条更老的通道（`m_adjLive`），两者未合并。

---

## 新增高通保留 / 运动模糊（第 14 批）—— 继续模板活

| 算子 | 类型 | 要点 |
|---|---|---|
| `HighPass` | 邻域（复用盒模糊） | `out = clamp(src - boxblur(src,r) + 128)` —— 平坦区呈中灰 128、边缘被保留。**模糊级直接复用 `BoxBlurFilterOp`**（与 USM 同套路），再次证明 `FilterOp` 族可组合：高通 = 源 + 负模糊 + 偏置。alpha 保留 |
| `MotionBlur` | 邻域（一维盒模糊） | 距离（1..128）+ 角度 **0°/45°/90°**；水平/垂直各一趟一维滑动窗口，45° = 先水平再垂直（**近似对角**，口径已在头文件与本文注明）。abyss=透明（与盒模糊同口径，边缘自然淡出） |

**验证**（新增用例 `hpblur`，9 条断言）：高通 == `clamp(src - 盒模糊 + 128)`（用生产盒模糊做参照，
模糊本身已由 `areafilter` 的朴素 oracle 钉住）、运动模糊三档 == 朴素一维盒、两者**分块投影 == 整幅**、
参数往返。全套 **19/19**；主程序干净重建 0 warning / 0 error。

**本轮踩坑（缺陷台账 #19）**：运动模糊初版把中间直通图 `Format_ARGB32` 直接赋回 `image`，
**换掉了管线约定的 `ARGB32_Premultiplied`** → oracle 报 `diffs=-1`（格式不匹配，像素其实算对了）。
**测试提交前抓到**。教训：滤镜出口的**格式也是契约**，中间缓冲可以是直通格式，写回必须转回预乘。

**仍欠**：① 运动模糊未做任意角度（只 0/45/90）与「角度 + 距离」的真实斜向采样；
② 高通未做「对比度/灰度化」选项；③ 滤镜菜单里仍有灰显占位（径向模糊/添加杂色/云彩等，
前者可套模板、后两者语义不同需另设计）。

---

## 新增最大值 / 最小值 / 自定滤镜（第 13 批）—— **算子框架的模板活**

**背景**：验证「加一个算子 = 8 步模板活、约 15–20 分钟」这个判断（见 [dev-plan.md](dev-plan.md) §5.1）。
三个算子**零新机制**，纯套 `FilterOp` 三段式 + 注册表 + 参数页 + 菜单 + 用例。

| 算子 | 类型 | 要点 |
|---|---|---|
| `Maximum` / `Minimum` | 排序统计滤波（与中值同族） | 窗口直方图取 100% / 0% 分位。比中值简单：**不维护 cum**，只维护当前极值 bin，进出样本时按需上下扫描。两算子合一个 `ExtremumFilterOp`（构造时带 Mode），`opsInit` 分别注册 |
| `CustomFilter` | 3×3 卷积 | `out = clamp(Σ w·in / scale + offset)`，alpha 原样保留。**一个算子顶多个**（改权重即得锐化/模糊/浮雕/边缘提取），并验证了「算子参数带数组」的路径 |

**验证**（新增用例 `morefilters`，10 条断言）：最大值/最小值 == 朴素极值 oracle（diffs=0）、
自定 == 朴素 3×3 卷积（diffs=0，含 scale=0 不除）、三者**分块投影 == 整幅**（无块界接缝）、
参数往返保真。全套 **18/18**；主程序干净重建 0 warning / 0 error。

**本轮踩坑（缺陷台账 #18）**：最小值初版 `delIn=false` 时跳过了「移除 abyss=0」，直方图里的 0
只进不出 → 整行最小值被拉成 0（oracle 差 1008 = 恰好整幅内部像素；最大值因忽略 0 而幸免）。
这是**台账 #10（中值 abyss 口径）的同一类坑**：滑窗更新的不变量是「样本数守恒」，进出样本
各 (2r+1) 个、越界一律按 0 照常加减。**oracle 在提交前抓到**，未外发。

**仍欠**：① `CustomFilter` 权重页面是 9 个裸 spinbox，未做 PS 那种 5×5 网格与「载入/存储」预设；
② 最大值/最小值未做 PS 的「保留（圆度/方形）」选项；③ 更多邻域算子（高通/运动模糊/添加杂色）可继续按模板补。

---

## 新增位移算子（第 12 批）—— **几何算子走专用路径**，不重建现有七算子

**背景**：这是 ROI 讨论里那个「装不进对称外扩」的算子（[engine/roi.md](engine/roi.md) §4/§5）。
本轮按**专用路径**落地：不补 ROI 的 ②③ 步，而是让几何算子**整幅求值**，从而把成本从
「12 个代码文件 + 重建 7 个已正确的面积算子」压到「新增 1 个算子 + 1 处判据」。

| 环节 | 决定 | 为什么 |
|---|---|---|
| 归类 | `Offset` **不是面积算子**（`inputPad()==0`），单列「几何算子」 | 它读的是**别处**的输入，不是邻居；用对称外扩逼近要按 `max(|dx|,|dy|)` 取输入（偏移 500px 时每块多取 276 倍），wrap 更是多矩形依赖 |
| 求值路径 | `FilterStack::hasGeometryOp()` → `Layer::compositeRasterRegion` 退化为「**整层求值一次 + 裁请求区**」（与「有启用样式」同一类取舍） | 逐块求值会在 64×64 块界错位；整幅只有一层判据，不动任何面积算子 |
| 数据流 | 它作为**合法节点进同一滤镜栈**（可开关/换序/撤销/进「滤镜」面板），只是让整栈走整层路径 | 复用已成熟的「非破坏滤镜栈」语义，不另造一套 |
| 算法 | `out(x,y) = in((x-dx) mod w, (y-dy) mod h)`；wrap（默认，PS 语义）与透明两种 abyss | 对照 `gimp:offset`（`GIMP_OFFSET_WRAP`）；未做 CLIP/LOOP/BLACK/WHITE |
| 实现 | 另开一张结果图，逐行两段 `memcpy`（wrap）/ 一段（透明） | 就地写会覆盖还没读的源像素；行内搬运是纯内存操作，比逐像素 `setPixel` 快一个量级 |
| 存储 | `.pslite` 在**固定块尾部追加** dx/dy/wrap | 与既有的「只在尾部追加、勿改顺序」口径一致 |
| UI | 复用 `AdjustmentPropsHost` 的程序化页（两个偏移滑块 + 环绕勾选）+ 滤镜→其它→位移 | 该 host 已有「按 OpName 切页 + 预览/提交信号」，不必新写面板；菜单项 `actionOffset` 原是灰显占位 |

**验证**：新增常驻用例 `offset`（12 条断言）——wrap/透明各 7 组参数 == **朴素取模 oracle**（diffs=0）、
dx=dy=0 不改画面、**分块投影 == 整幅合成（diffs=0，几何算子最要紧的护栏）**、往返保住 dx/dy/wrap。
全套 **17/17**；主程序干净重建 0 warning / 0 error。

**仍欠**：① CLIP/LOOP/BLACK/WHITE 等其它 abyss 策略；② 位移**之后**再接面积算子（栈序为
「面积算子 → 几何算子」时走整层路径没问题，但几何算子在中间时每节点需要各自的几何 —— 那是
ROI 第 ③ 步的范围）；③ 大文档下整层求值仍是同步的（不参与异步定稿：该链路判据是 `inputPad>0`）。

---

## ROI 通用化：**第 ① 步已落地，②③ 与 offset 绑定后置**（第 11 批）

**结论先行**：完整三步的代价是 **12 个代码文件**（含 7 个**当前完全正确**的面积算子内环）＋测试与文档，
而收益只有「以后几何算子能装进来」；且做完之后 **wrap-around 仍装不下**（多矩形依赖）、栈里仍只能有
一个几何算子且必须在最后。故本轮**只落第 ① 步**（契约带几何，零行为变化），②③ 与「真的要做 offset」
一起做。判据与数据见 [engine/roi.md](engine/roi.md) §5。

**第 ① 步实际做了什么**（细节见 roi.md §5「① 落地说明」）：

| 环节 | 改动 |
|---|---|
| 契约 | `FilterContext` 增 `QRect srcRect / outRect`（**输入图本地坐标**） |
| 传参 | `FilterStack::apply(source, srcRect, outRect, shouldAbort)` / `FilterEval::applyNode(image, node, srcRect, outRect, shouldAbort)`，入参用**文档坐标** |
| 换算 | `PointFilterOp` 一处负责「文档坐标 → 本地坐标」（减 `srcRect.topLeft()`） |
| 真实路径 | `Layer::compositeRasterRegion` 把 `(srcWant, want)` 按文档坐标传下去 |
| 算子 | **七个面积算子一行未改**（它们只依赖「手里这张图」+ `inputPad`） |
| 验证 | 新用例 `roictx`：用**测试探针**（`setFilterGeometryProbe`，生产恒 null）把两条路径的几何真值钉住，并断言「带几何 + 裁回 == 不带几何求值后裁回」逐像素一致 |

**验证**：全套 **16/16**；主程序干净重建 0 warning / 0 error。

**本轮踩到的两个坑**（台账 #16/#17）：坐标空间一度混用（`srcRect` 文档、`outRect` 本地），
而 `applyNode` 的 `contains` 断言在 **Release 下不生效** → 静默算错一块；
以及 `roictx` 的**期望值本身写错**，一度误判实现有错。教训：跨坐标系的值只留**一处**换算，
并且断言消息必须同时打印「期望 / 实际」。

**仍欠**：② 裁剪按几何（图路径补齐「输出覆盖请求区」）—— 图路径目前输出仍与输入同尺寸；
③ 算子契约改 `process(src, out, ctx)`；两件都与 offset 一起做。

### 三步设计（原文保留，供将来执行时照抄）

| 步骤 | 内容 | 验收标准 | 状态 |
|---|---|---|---|
| ① 契约带几何 | `FilterContext` 增 `srcRect` / `outRect`；两条路径填真值；直调路径保持「src = out = 图像自身」 | 七算子行为不变、全套测试绿 | ✅ 已落地 |
| ② 裁剪按几何 | `compositeRasterRegion` 改用 `FilterStack::requiredInputRect(want)`；裁剪点改为 `want.topLeft() - srcWant.topLeft()`；**图路径补齐裁回请求区** | 测试仍绿 | ⏸ 与 offset 一起 |
| ③ 算子契约改 `process(const QImage &src, QImage &out, ctx)` | 算子从 ctx 读映射、自己写输出 | 破坏性：七处内环取样基准都要改 | ⏸ 与 offset 一起 |

对照 GEGL：`get_required_for_output(op, pad, output_roi) → GeglRectangle`（通用矩形）对应本项目
现在的 `FilterNode::inputPad() → int`（对称标量）。要补的就是把它升级为
`requiredInputRect(want)`，并在 §3 的两条路径上都用起来。

---

## 新增锐化算子（第 6 批）+ **一个必须先补的架构缺口：ROI 只能表达「对称外扩」**
它要动的是**算子契约**（不只是调用方）：六个面积算子的内环取样基准、像素层/图两条路径、全部面积测试。
更麻烦的是**多算子栈**（见下）。在验证范围之外动手，把绿着的 13/13 搞红的概率很高 —— 所以先把设计钉死。

### 目标形态（对照 GEGL）
| | GEGL | 本项目现状 |
|---|---|---|
| 输入需求 | `get_required_for_output(op, pad, output_roi) → GeglRectangle`（**通用矩形**） | `FilterNode::inputPad() → int`（**对称标量**） |
| 算子拿到什么 | `process` 里给的 `roi` 是**输出矩形**，输入按需索取 | 拿到**整幅补边图**，就地处理 |
| 谁来裁剪 | 算子自己按映射写输出 | 调用方按 pad **取中心** |

### 迁移设计（三步，每步可单独验证）
1. **契约带上几何**：`FilterContext` 增加 `QRect srcRect; QRect outRect;`（层/文档坐标）。
   `Layer::compositeRasterRegion` 与 `PointFilterOp::process` 填真值；
   `FilterEval::applyNode(image, node)` 的直调路径保持「src = out = 图像自身」语义（测试用）。
   **此步六个算子行为完全不变** ⇒ 全套测试必须保持绿（这是本步的验收标准）。
2. **裁剪按几何**：`compositeRasterRegion` 用新的 `FilterStack::requiredInputRect(want)`
   取代 `want.adjusted(-pad,-pad,pad,pad)`；裁剪点由「按 pad 取中心」改为 `want.topLeft() - srcWant.topLeft()`。
3. **算子契约改为 `process(const QImage &src, QImage &out, ctx)`**：算子从 ctx 读映射、自己写输出。
   —— **破坏性改动**，六个算子都要改内环取样基准。

### 多算子栈的额外门槛（别低估）
若栈里含非 pad 类算子（如 offset），**每个算子都需要各自的中间缓冲与几何**，
不能像现在这样「所有滤镜依次作用在同一张补边图上」—— 这正是 GEGL 让**每个节点各持一个 buffer** 的根本原因。
⇒ 补第 3 步之前先决策：**是否接受「一个栈里最多一个几何类算子、且必须在最后」**这种受限语义
（可以先上 offset，代价是记一条明确限制）。

### 本轮实际交付：浮雕（3×3 方向性核 + 中灰偏置）
| 决定 | 为什么 |
|---|---|
| 核 `[-s,-s,0; -s,0,s; 0,s,s]`，**权和 = 0**，再统一 `+128` | 平坦区域因此呈**中灰 128**、两侧一亮一暗 —— 这才是「浮雕」的观感（权和为 1 会退化成普通锐化） |
| alpha **不参与卷积**、原样保留 | 对 alpha 做浮雕只会造出半透明的「浮雕边缘」，没有意义。与「模糊类连 alpha 一起滤」是刻意差异 |
| 强度 1..4 只缩放核（中灰偏置不变） | 强度不该把「中灰底」也一起放大 |
| 再次复用 2 行滚动窗口 | 与 `FindEdges` / `Sharpen` 同构，O(w) 内存、不做整图拷贝 |

**验证**（`areafilter` 扩到 7 个算子）：浮雕 s=1 / s=2 == 朴素 3×3 方向性核（语义 oracle）diffs=0；
分块投影 == 整幅合成 diffs=0；强度往返保住 2。全套 13/13；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① ROI 通用化（上面三步）→ 之后才能上 offset（含 wrap）；② ~~中值找中值的增量 cum 优化~~（**已完成**）；
③ ~~异步/可取消的定稿~~（**已落地**，见「滤镜定稿的异步/可取消（第 10 批）」）。

---

## 图层滤镜栈面板：让「非破坏」真的可见可管（第 8 批）

**为什么做**：滤镜一直宣传是**非破坏**的层滤镜节点，但界面上**看不见也管不了** ——
给图层应用三个滤镜之后，既不能单独关掉一个、也不能删除或改参数，只能靠 Ctrl+Z（继续编辑就没了）。
而 domain 侧的能力**早就齐了**：`setLayerFilterEnabled` / `removeLayerFilter` / `setLayerFilterNode(pushUndo)` / `snapshot+replaceAll`。
所以这是纯 UI 缺口，不是引擎缺口。

| 决定 | 为什么 |
|---|---|
| 放进**属性面板**（`FilterStackWidget` 插在调整层参数下方），不新开 dock | 功能上它就是「活动图层的属性」；省掉 .ui + 菜单 + 面板注册一串改动，与调整层参数并列也更自然 |
| 列表行带**参数摘要**（「盒模糊（半径 3）」「锐化（强度 2）」） | 否则一行只有「锐化」「锐化」，看不出区别 |
| 新增 `ImageDocument::moveLayerFilter(from,to)`（快照 → `QVector::move` → `replaceAll` + 一条 undo + 标脏） | 顺序影响结果，换序必须可撤销；domain 侧原先没有这个入口 |
| 勾选**同值不提交** | 沿用 spinbox 那条教训：Qt 会重发同值信号，白留一条空操作 undo |
| 编辑已有滤镜**也有实时预览** | 面板不持有画布 ⇒ 预览区域走「文档里的**当前可见区提示**」（`ImageDocument::setPreviewRegionHint`，由画布在更新视口时写入；`region` 传空即回落到它），再按 80ms 节流。**取消时用整层标脏写回原参数** —— 预览只刷过可见区，屏幕外还是预览值 |
| 无滤镜时整个面板隐藏 | 不占属性面板的地方 |

**测试抓到的真 bug（缺陷台账 #7）**：按钮可用性原先只在 `refresh()` 里算，
**选中行变化时不更新** → 选中第 2 行后「上移」仍是灰的（功能不可达）。
修法：抽出 `updateButtons()`，并连 `currentRowChanged`。
这条是测试**在提交前**抓到的 —— 正是「面板测试必须真的驱动 domain，而不是只看画出来」的价值。

**可发现性修正（用户实测反馈）**：第一版把面板插在属性面板**最底部**、且**无滤镜时整个隐藏** ——
结果用户直接问「你说的面板在哪里」。这是设计错误：**找不到的功能等于没有**。改法两条：
① 插到属性面板**最前面**（不用滚动）；② 无滤镜时**不隐藏**，给一行占位提示「（暂无滤镜：用「滤镜」菜单应用后会出现在这里）」，
按钮置灰。测试同步改为断言「无滤镜时仍显示 + 有占位提示 + 按钮禁用」。

**验证**（新增常驻用例 `filterpanel`，14 条断言）：
列表行数与参数摘要 ✓；勾选 ↔ 文档启用状态（含取消/重新勾选）✓；上移后顺序真的变成「锐化在最前」✓；
删除后文档与列表同时少一个、且一条撤销能撤回 ✓；**空 region 预览回落到可见区提示（区内 4018 / 区外 0）** ✓；无滤镜时面板隐藏 ✓。
全套 **14/14**；主程序干净全量重建 0 warning / 0 error。

**仍欠**：① 面板不显示**蒙版/混合**对滤镜的影响；② 拖放换序（现在只有上移/下移按钮）；
③ 「可见区提示」是**提示**不是契约：过期最多导致多算/少算一块可见区，不影响正确性（定稿都整层标脏）——
但若将来出现非模态的滤镜面板，得改成画布主动推；
④ 调整层仍不能挂面积算子（ROI 通用化那条）。

---

## 参数 UI 的形态：PS / GIMP / 本项目对照（含一条待办：把对话框改成非模态）

**为什么记这个**：本项目现在的滤镜参数是**模态对话框**（`FilterDialog`）；为了绕开「模态吞掉主窗口 Ctrl+Z」，
还专门打了一个补丁（对话框自带参数撤销栈）。对照两家产品后发现：**模态是可选的，而且不是更好的那条路**。

| | 参数 UI 形态 | 滤镜栈 UI | 备注 |
|---|---|---|---|
| **PS 普通滤镜** | **模态对话框**（参数 + 预览 + 确定/取消） | —— | 滤镜库（Filter Gallery）也是对话框：左缩略图库 + 右参数区 |
| **PS 智能滤镜** | 双击行再开对话框；（较新版本）属性面板可显示选中项 | ✅ **嵌在「图层」面板里的子行**：开关 / 双击改参 / **拖动换序** | 栈 UI 归图层，不归属性面板 |
| **GIMP 2.10 GEGL 滤镜** | **非模态**：参数进**工具选项**停靠栏 | ❌ 无滤镜栈列表；管理基本靠**撤销历史**（完整 NDE UI 未完成） | 源码依据：`app/tools/gimpgegltool.c` 中 `GimpGeglTool` 派生自 `GimpOperationTool`，用 `filter_tool_class->dialog = gimp_gegl_tool_dialog` 把参数 UI 填进工具选项框（`options_gui` / `options_box`） |
| **本项目** | **模态对话框** + 对话框内 Ctrl+Z 参数撤销 | ✅ 属性面板**顶部**的栈列表（开关 / 双击改参 / 上下移 / 删除） | 栈 UI 更接近 PS 智能滤镜；参数 UI 比 GIMP **退了一步** |

### 已落地：`FilterDialog` 改为非模态

理由三条（对照后确立）：
1. GIMP 那条证明「滤镜参数不必模态」；非模态时拖参数**还能缩放/平移画布**看细节
   （我们的区域化预览本来就按可见区刷新，视口变化天然跟得上 —— 画布在更新视口时写「可见区提示」）；
2. **补丁的根因就是模态**：原来模态会吞掉主窗口快捷键，所以给对话框加了自带 Ctrl+Z；
3. 成本可控。

改法：`exec()` → `new FilterDialog(...)` + `WA_DeleteOnClose` + `show()`，
确定/取消改接 `accepted()` / `rejected()`（非模态拿不到 `exec()` 的返回值），
**预览节流计时器从调用点的局部变量挪进对话框**（非模态下调用方早就返回了，局部 QTimer 会立刻析构），
两处调用点（`MainWindow::runFilterDialog`、`FilterStackWidget::onEdit`）改成信号驱动。
顺带去掉了一处耦合：菜单路径不再需要画布句柄（预览 region 传空即回落到可见区提示）。

**保留对话框内 Ctrl+Z（与现实对齐的更正）**：非模态后主窗口的撤销键**能**到达，
但它会去撤销**上一次已提交**的文档操作，而本次滤镜还没提交 —— 那不是用户按 Ctrl+Z 时想要的「回退刚拖的参数」。
所以对话框自带的那条继续留着（只回退参数），非模态的真正收益是**参数调节期间画布可交互**，不是这条快捷键。

**仍欠 / 风险**：① 非模态下对话框与文档生命周期解耦了 —— 若对话框开着时文档被关闭，
调用方 lambda 里捕获的 `doc` 会悬空（**当前未防**，属已知风险，将来可接 `documentChanged` 关闭对话框）；
② 对话框「确定」后没有自动聚焦回画布。

---

## 未排期：进一步对齐 GIMP 时可按模块补

若要以 GIMP Unified Transform 为齐，建议分轮，不必与上面绑在一起：

1. **约束** — Shift/修饰键与手柄约束（等比、沿边斜切、透视约束等）
2. **预览选项** — 是否显示预览、合成预览、同步、透明度等
3. **变换对象** — 选区 / 路径等，不只活动层像素
4. **clip** — 变换结果裁到画布 / 按结果调整层等策略 UI

---

## 滤镜链后端调度 + GPU 中值（先接上）

**已实现**：`FilterStack::apply` → `RasterScheduler` → `CpuRasterBackend` / 可选 `GpuRasterBackend`
（QRhi D3D11 compute，仅全中值链）；成本表 / 熔断 / `PSLITE_RASTER`；`rasterbackend` + 门控
`gpumedian`。规格见 [gpu-plan.md](gpu-plan.md)。

**产品决策**：spike 端到端约 **0.9～1.1×**（插入排序时代，未达 ≥3×），仍接上路径但不声称加速。
详见 `gpu-plan.md` §7.2.1 / §7.2.1.1。

**已收口（T1.7–T1.16）**：
1. **kernel** → bitonic（T1.7）→ **计数选择**（T1.13；两级 16 箱）；
2. **池化 + shader 缓存** → dst/ubuf 只增不减；`cachedCs`（T1.15）；src 每帧 Immutable 重建；
3. **默认策略** → `Auto` = CPU；GPU 仅 `PSLITE_RASTER=gpu`；
4. **仓库卫生**（T1.14）→ `psDemo/tools/median_e2e/` + `median_e2e.pro` 树内可构建；
5. **重测**（§7.2.1.4）：r=3 ≈ **1.9～2.5×**；r=1 ≈ **3.2×**；bit-exact。
   ⇒ **保留 GPU 中值路径；维持 opt-in；不开 P2**。bitonic 时代 0.53× 见 §7.2.1.2。

**已收口（P1′ / T2.0–T2.9）**：`custom.comp` + `gpucustom` + `tools/custom_e2e/`；
标准测法（seed=7 / warmup=2）：锐化 **2.5～3.4×**；稠密盒 **2.6～2.8×（未跨 3×）**；
Custom ≈48 ns/px、Gauss ≈19。Q11–Q13 关闭。维持 opt-in。

**已关闭**：T4.1 / T4.2 / **T4.3**（解预乘进 shader；锐化 **4.4～7×**，稠密 **2.8～4.4×**，中值 ~2.1×；门控 bit-exact）。

**仍欠**：
0. T4.4 独显 / 无 GPU 机验证。
1. T4.5 成本表 `187` vs 实测。
2. T4.6 GLES2 回退说明或去掉。
3. ~~★ **T4.7** 近零成本扩覆盖~~ ✅（Sharpen/Emboss → custom；Max/Min → median+kth；FindEdges 非线性未进）。
4. T3.0 `AddNoise` / T3.1 Gauss —— **禁止整表铺开**。

---

## 瓦片页缓存（Tile Page Cache）—— **未做，评估完成**

**现状**：本工程有瓦片化存储（64×64 稀疏 `QHash<qint64,QImage>`）与「脏矩形 + 分块缓存 + 视口优先 +
空闲渐进」的投影（当年就照 GIMP 做的），但 **`tilebuffer.h:30` 自己写着「本 Demo 去掉 COW / 交换 /
全局空瓦片池」** ⇒ **瓦片常驻内存无上限、无淘汰、无换页**，随触碰的格子数**单调增长**。

**已经是有界的（不用动）**：
- `HistoryStack` 有 256 MB 上限（但**靠丢最老历史**实现 —— 丢了就撤不回去）
- `Layer::m_regionFilterImage` 是**单槽**区域滤镜缓存（不泄漏）
- 投影有 chunk 有效位与空闲渐进；但 `Projection::m_buffer` 是**整画布稠密图**（不可淘汰）

**值得做，但要小做**：只补「**交换**」这一件 + 配套的内存预算与可观测。
**明确不能照搬 GIMP 的 per-tile `ref_count`**：本工程 `TileBuffer::ensureTile/tileAt`
把 **`QImage*` 裸指针**交给调用方并长期持有，per-tile 淘汰会**悬垂**
（症状就是台账 #21 那类随机 `0xC0000005`）。
⇒ 改用**粗粒度「钉住整个 buffer」+ 只在静默点淘汰**，API 不变、改动集中。

**仍欠**：
1. A 档：可观测 `PSLITE_MEM=1`（tiles/undo/projection/swap/peak 五项）+ 预算记账 + 淘汰**可重算的**派生缓存（1–2 天）；
2. ★ **检查点 P4**：实测「复制图层」的内存增量 —— 若 ≈ 整层，说明有代码用整层非 const 访问
   **打断了 `QImage` 的隐式共享**，**修它比做页缓存便宜得多**，应先修；
3. B 档：瓦片磁盘换页（4–7 天）；
4. undo 上限做成**可调 + 超限提示**（半天；**替代**「undo 瓦片化」这个 5–10 天的大改）。

**完整计划（GIMP 主逻辑裁剪、三态瓦片、swap 文件设计、四个静默点、验收与风险）见
[page-cache-plan.md](page-cache-plan.md)。**

---

## 维护

- 新发现「已有功能的坑」：在本文件追加一节（已实现 / 已知风险 / 完善方向 / 相关代码）
- 某项完善落地后：把该节改为「已缓解」并链到对应实现说明，或删除风险段并在 [features.md](features.md) 更新口径
