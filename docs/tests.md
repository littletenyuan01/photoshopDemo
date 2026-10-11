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

> 当前 **19 个用例**：`pixelprim / filterchain / regionraster / composite / graphcheck / projectio /
> adjustpanel / adjustapply / blendparallel / transformlive / dirtybudget / areafilter /
> filterundo / filterpanel / filterasync / roictx / offset / morefilters / hpblur`。

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
| `roictx` | ROI 第①步：用**测试探针**钉住两条求值路径的几何真值（直接/图路径 = 整张图；像素层路径 = 文档坐标下的「所需输入区/请求区」换算成本地坐标）；几何如实描述手里那张图；**带几何 + 裁回 == 不带几何求值后裁回**（逐像素） |
| `offset` | 位移（**几何算子**）：wrap / 透明各 7 组参数 == **朴素取模 oracle**；dx=dy=0 不改画面；**分块投影 == 整幅合成**（几何算子若被逐块求值必在块界错位，这是最要紧的护栏）；栈被识别为「含几何算子」；往返保住 dx/dy/wrap |
| `morefilters` | 最大值/最小值 == 朴素极值 oracle、自定 == 朴素 3×3 卷积（含 scale=0）；三者**分块投影 == 整幅**（无块界接缝）；参数往返保真 |
| `hpblur` | 高通 == `clamp(src - 盒模糊 + 128)`（用生产盒模糊做参照）；运动模糊 0°/45°/90° == 朴素一维盒；两者**分块投影 == 整幅**；参数往返保真 |

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
