# tests — 常驻测试

无 GUI 断言：不需要人工看画面，跑完给退出码。代码有更新时同步本文件（见 `docs/README.md` 维护约定）。

## 定位

守住几类**容易被后续改动悄悄破坏**的性质：逐像素等价、存读保真、并行与串行一致、时间上界。

不做的事：不替代人工手感验收（拖拽跟手度、渲染观感）；不做像素级 UI 视觉回归。

## 构建与运行

```bat
mkdir build-tests && cd build-tests
qmake <repo>\psDemo\tests.pro && mingw32-make -j4
release\psdemo_tests.exe                          :: 跑全部
release\psdemo_tests.exe composite regionraster   :: 只跑指定用例
```

退出码 = **失败用例数**（0 = 全过，便于脚本 / CI 判定）。构建目录 `.build-tests/` 已在 `.gitignore`。

> **工程文件为什么在 `psDemo/` 而不是 `psDemo/tests/`**：qmake 里 `.pri` 的**相对路径按「包含它的 `.pro` 所在目录」解析**（不是 `.pri` 自己的目录），
> `app.pri` 里 `domain/layer.cpp` 这类路径只有在 `.pro` 与 `app.pri` 同目录时才成立。
> 测试**源文件**仍在 `psDemo/tests/`。

## 用例

> 当前 **28 个用例**：`pixelprim / filterchain / regionraster / composite / graphcheck / projectio /
> adjustpanel / adjustapply / blendparallel / transformlive / dirtybudget / areafilter /
> filterundo / filterpanel / filterasync / filterpreview / previewdown / posasync / jobteardown /
> roictx / offset / morefilters / hpblur / noise / batcha / ripple / rasterbackend / c1pixelate`。

| 名字 | 守住什么 |
|---|---|
| `pixelprim` | 逐像素合成原语的 Normal 内联快路径与改动前旧实现**位等价**：穷举 27 模式 × 56×56 像素对 × 3 不透明度 × 4 蒙版 × 2 坐标 = **2,032,128 组** |
| `filterchain` | 滤镜链走图（就地求值）与直接 `FilterEval` 循环逐像素一致；附带打印建图成本 |
| `regionraster` | 层复合栅格「按区域滤镜」与「整层滤镜后裁剪」逐像素一致 |
| `composite` | 投影分块**并行**合成与单次整体合成逐像素一致（文档含调整层 + 带滤镜层：守住曾导致并行访问冲突崩溃的分支） |
| `graphcheck` | 图引擎自检（结构 / 缓存复用 / 脏传播 / 与直接合成交叉校验） |
| `projectio` | `.pslite` 存读往返：层 kind / 属性 / 调整层与滤镜参数逐项一致，且**整幅合成逐像素一致** |
| `adjustpanel` | 调整层面板：已存参数回填滑动条（含 gamma 量程）；改值只预览、交互结束提交一次且不重复；含**盒模糊半径页**（程序化建页）的回填 |
| `adjustapply` | 调整层参数落盘后重新打开**仍作用于画面** |
| `blendparallel` | `blendLayerRangeParallel` 与串行版逐像素一致（移动工具逐帧路径） |
| `transformlive` | 自由变换 live 投影与提交后合成一致（identity 严格相等 / 缩小 50% 千分之一内） |
| `dirtybudget` | 带滤镜层上 64×64 脏区重投影的时间上界（区域化前约 280ms → 现在约 2ms） |
| `areafilter` | 面积算子（**盒模糊 / 高斯 / 中值 / 锐化 / 浮雕 / USM / 查找边缘**）：图路径 ROI 外扩 == 直接求值；像素层分块 == 整幅（无跨块接缝，七个算子都验）；半径/强度 0/护栏/往返；**语义 oracle**（盒模糊 == 朴素窗口、查找边缘 == 朴素 3×3 Sobel、中值 == 朴素排序中值且**覆盖 r=1..4**、锐化 == 朴素 3×3 卷积、浮雕 == 朴素 3×3 方向性核）；**精度量化**（三次盒模糊 vs 精确高斯：内部最大单通道误差，断言 ≤8/255、实测 3）；**中值成本实测**（512² r=3 ≈49 ms / ~186 ns 每像素；增量 cum 前后 2.8×，见 pending-dev）；**FilterOp 三段式 + 每线程常驻实例协议** |
| `filterundo` | 滤镜对话框：撤销原子性（确定 → **一次**撤销即回到原样；取消 → 状态原样且不改变撤销栈可用性）；**区域化预览**（区内已刷新、区外未重算） |
| `filterpanel` | 图层滤镜栈面板：列表行含参数摘要；勾选↔启用（同值不提交）；上移真改顺序；删除真的删且可撤销；**空 region 预览回落到可见区提示**；**无滤镜时不隐藏**（占位提示 + 按钮禁用）；选中变化会刷新按钮可用性 |
| `filterasync` | 滤镜**定稿的异步/可取消**链路：协作式取消真的中断面积算子（谓词被调用、结果是半成品）；区域滤镜缓存命中且与同步路径逐像素一致；层内容改动使世代号前进、缓存失效；**端到端**（离屏画布触发 → 后台任务 → 缓存就位 → 命中缓存的投影 == 整幅合成） |
| `filterpreview` | 滤镜**预览的异步化**：静默标脏（发 `layerFilterPreviewChanged` 但**不发** `contentChanged`）；后台任务只算给定区域；命中缓存的区域 == 整层滤镜后裁同块 |
| `previewdown` | 预览**降采样**：空间参数按倍数缩小（0 保持 0、钳到最小 1、非空间参数不动）；任务照常写缓存且尺寸正确；降采样结果与原分辨率**确实不同**且**大体相近** |
| `noise` | 添加杂色的**确定性**：同参可复现、换种子图案不同、数量 0 恒等、全透明像素不受影响；单色=三通道同扰动、非单色出彩噪；**位置稳定**（请求区域与整层返回同一张图、文档原点一致 —— 逐块求值会让它变红）；高斯同样可复现；含种子的往返 |
| `batcha` | 滤镜批次 A：Solarize oracle；蒙尘阈值 255/0 边界；减少杂色强度 0/100；锐化边缘平坦内部恒等；三分块投影==整幅；reduceNoiseStrength 往返 |
| `ripple` | 波纹：朴素正弦位移 oracle；振幅 0 恒等；requiredInputs 左右扩；hasPositionalOp；分块投影==整幅；参数往返 |
| `roictx` | ROI 第①步：用**测试探针**钉住两条求值路径的几何真值（直接/图路径 = 整张图；像素层路径 = 文档坐标下的「所需输入区/请求区」换算成本地坐标）；几何如实描述手里那张图；**带几何 + 裁回 == 不带几何求值后裁回**（逐像素） |
| `offset` | 位移（**不做环绕**）：六组参数（正负 / 单轴 / 远大于尺寸）== 朴素 oracle；`requiredInputs` 形状（输出区 ∪ 平移后的输出区，dx=dy=0 时退化为输出区本身）；**分块投影 == 整幅合成**；**位移+中值同栈**也不能块界错位；dx=dy=0 不改画面；往返保住 dx/dy |
| `morefilters` | 最大值/最小值 == 朴素极值 oracle、自定 == 朴素 3×3 卷积（含 scale=0）；三者**分块投影 == 整幅**（无块界接缝）；参数往返保真 |
| `hpblur` | 高通 == `clamp(src - 盒模糊 + 128)`；运动模糊 0°/90° 一维盒、30°/45° 方向最近邻取样平均 == 朴素 oracle；各角「分块投影 == 整幅」；`requiredInputs` 按 `|d·cos|/|d·sin|` 外扩；参数往返 |
| `posasync` | **位置相关算子**（添加杂色）的预览异步化：want 被放大成**整层**、任务把整层写进区域缓存、域层复用该缓存把结果还给请求方（否则等于在 UI 线程上再整层重算一遍） |
| `jobteardown` | **异步任务的析构安全**：反复「起了重任务立刻析构」，守住 `~LayerFilterJob` 必须等在途 future（曾因注释写了假不变量而偶发堆损坏，见缺陷台账 #21） |
| `rasterbackend` | 滤镜链**后端调度（P0）**：默认 CPU bit-exact；`PSLITE_RASTER=cpu` 不碰 GPU；Failed 降级正确；Failed×3 熔断；**CPU 补算成功不得清零**；Cancelled / supports=false 不计熔断；决策边界（点算子/像素门槛/位置相关/位移）；成本表覆盖全部 `OpName`；带几何重载；Failed 无副作用 |
| `gpumedian` | **有 GPU 才跑**：`PSLITE_RASTER=gpu` 且后端非 `cpu` 时比对中值 vs CPU（容差逐通道 ≤1）；否则跳过不算失败 |
| `c1pixelate` | C1 马赛克/碎片：方块均值 / 四角偏移 oracle；马赛克 hasPositionalOp；分块投影==整幅；参数/op 往返 |

## 约定

- 每个用例实现为 `int tXxx()`，返回**失败项数**；断言用 `PsTests::Reporter::check`，
  机器相关的量（耗时 / 帧率）用 `info` 只打印、不断言。
- 时间类断言只作**粗护栏**（阈值留十几倍余量）：本机单次运行噪声可达 ±2×。
- 需要 QApplication 的用例：`testmain.cpp` 在未指定 `QT_QPA_PLATFORM` 时自动设 `offscreen`，
  保证无桌面环境也能跑。
- fixture 一律**自建**，不依赖仓库外的文件。

## 写测试时踩过的坑（别重犯）

1. **同名控件要按「当前页」查**：调整层面板多个页面都有 `slider_gamma`（色阶 / 曝光），
   对 host 全树 `findChild` 会命中别的页，测出假值。
2. **信号 lambda 别每轮重连 + 按引用捕获局部计数器**：旧 lambda 会写悬垂引用，计数翻倍。
   要么只连一次，要么按增量断言。
3. **别用「初值表示失败」的哨兵再只在失败时更新**：例如 `int worst = -1; if (d) worst = d;`
   在全通过时会残留 −1 被误判为失败（本套件的 `composite` 就踩过，现用 `anyBad` 布尔）。
4. **异步用例要等，但不要盲等**：`filterasync` 用 `QApplication::processEvents()` 轮询
   「缓存是否就位」并带 3 秒上限 —— 固定 `sleep` 既慢又不稳。
5. **崩溃/断言会吞掉已打印的 PASS**：`testmain` 现在把 `stdout` 设成**无缓冲**
   （`setvbuf(..., _IONBF, ...)`）。早先断言触发时整段输出丢失，定位全靠猜。
