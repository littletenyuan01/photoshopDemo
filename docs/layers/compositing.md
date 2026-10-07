# 合成、透明度与视图刷新

## 1. 合成器做什么

`Compositor` **只读**各层像素，输出一张预览图，不修改层数据。

流程（本项目）：

1. 建一张与文档同大的**临时结果图**，先填透明
2. 自底向顶遍历可见层
3. 每层与「下方已合成结果」按**混合模式 + 不透明度**逐像素混合（可限定脏矩形）
4. 返回结果；`CanvasView` 再按缩放画到屏幕（棋盘格表示透明）

不是矢量「布尔挖洞」，而是 **Alpha 混合**：半透明会透出下层颜色。

代码分工（与 GIMP 一一对应）：

| 本项目 | GIMP | 干什么 |
|---|---|---|
| `engine/blend.cpp` | `app/operations/layer-modes/gimpoperationlayermode-blend.c` | 逐模式的颜色合并 `B(Cb, Cs)` |
| `engine/compositor.cpp` | `.../gimpoperationlayermode-composite.c` | Alpha 合成（`composite_union`） |
| `domain/blendmode.h` | `app/operations/operations-enums.h` | 模式枚举 |

## 2. 小例子

文档 4×3，背景全白；上层 2×2 不透明红，贴在右上（模式 = 正常）：

```
结果：
W W R R
W W R R
W W W W
```

若上层红且 **α=50%**：那 4 格是粉红（红与白各半），不是把白挖掉。

## 3. 两层公式：先算颜色，再算 Alpha

合成拆成两步，**顺序不能反**（GIMP 也是这个结构）：

```
① 颜色合并：comp = B(Cb, Cs)        ← 取决于混合模式，逐模式一张公式表
② Alpha 合成：把 comp 与背板按 Alpha 加权求和
```

- **背板 `Cb` / `αb`**：临时结果里已有的内容（`in`，更底层已混入）
- **上层 `Cs` / `αs`**：正在盖上去的当前层像素（`layer`）
- 颜色一律先转成**直通（非预乘）**再算，最后写回预乘存储

### 3.1 颜色合并 `B(Cb, Cs)`（27 种模式）

公式全部来自 `gimp-master/app/operations/layer-modes/gimpoperationlayermode-blend.c`，
下面 `Cb`/`Cs` 均为 0~1 的直通值。**顺序 = PS 图层面板的分组顺序 = `Ps::BlendMode` 枚举序 =
`.ui` 模式项顺序**（combo 另有运行时分隔线，靠 `itemData` 映射，勿用 combo 下标当模式）。

| # | 模式（PS） | 枚举 | GIMP 函数 | `B(Cb, Cs)` |
|---|---|---|---|---|
| 0 | 正常 | Normal | `blend_normal` | `Cs` |
| 1 | 溶解 | Dissolve | `gimpoperationdissolve.c` | 逐像素随机阈值：保留 → `Cs` 且 α=1，否则保留下层 |
| 2 | 变暗 | Darken | `darken_only` | `min(Cb, Cs)` |
| 3 | 正片叠底 | Multiply | `multiply` | `Cb·Cs` |
| 4 | 颜色加深 | ColorBurn | `burn` | `1 − (1−Cb)/Cs` |
| 5 | 线性加深 | LinearBurn | `linear_burn` | `Cb + Cs − 1` |
| 6 | 深色 | DarkerColor | `luma_darken_only` | `Lum(Cb) ≤ Lum(Cs) ? Cb : Cs`（整像素取舍） |
| 7 | 变亮 | Lighten | `lighten_only` | `max(Cb, Cs)` |
| 8 | 滤色 | Screen | `screen` | `1 − (1−Cb)(1−Cs)` |
| 9 | 颜色减淡 | ColorDodge | `dodge` | `Cb/(1−Cs)` |
| 10 | 线性减淡（添加） | LinearDodge | `addition` | `Cb + Cs` |
| 11 | 浅色 | LighterColor | `luma_lighten_only` | `Lum(Cb) ≥ Lum(Cs) ? Cb : Cs` |
| 12 | 叠加 | Overlay | `overlay` | `Cb<0.5 ? 2CbCs : 1 − 2(1−Cs)(1−Cb)` |
| 13 | 柔光 | SoftLight | `softlight` | `(1−Cb)·CbCs + Cb·(1 − (1−Cb)(1−Cs))` |
| 14 | 强光 | HardLight | `hardlight` | `Cs>0.5 ? 1 − (1−Cb)(1 − 2(Cs−0.5)) : 2CbCs` |
| 15 | 亮光 | VividLight | `vivid_light` | `Cs≤0.5 ? 1 − (1−Cb)/(2Cs) : Cb/(2(1−Cs))` |
| 16 | 线性光 | LinearLight | `linear_light` | `Cb + 2Cs − 1`（分段写法，两段等价） |
| 17 | 点光 | PinLight | `pin_light` | `Cs>0.5 ? max(Cb, 2(Cs−0.5)) : min(Cb, 2Cs)` |
| 18 | 实色混合 | HardMix | `hard_mix` | `Cb + Cs < 1 ? 0 : 1` |
| 19 | 差值 | Difference | `difference` | `|Cb−Cs|` |
| 20 | 排除 | Exclusion | `exclusion` | `0.5 − 2(Cb−0.5)(Cs−0.5)` |
| 21 | 减去 | Subtract | `subtract` | `Cb − Cs` |
| 22 | 划分 | Divide | `divide` | `Cb/Cs` |
| 23 | 色相 | Hue | `hsv_hue` | 取 `Cs` 的色相，饱和度/明度按 `Cb` 的相对关系缩放 |
| 24 | 饱和度 | Saturation | `hsv_saturation` | 取 `Cs` 的饱和度，色相/明度按 `Cb` |
| 25 | 颜色 | Color | `hsl_color` | 取 `Cs` 的色相+饱和度，保留 `Cb` 的明度 |
| 26 | 明度 | Luminosity | `luminance` | `Cb × (Lum(Cs)/Lum(Cb))` |

几条容易踩的点：

- **除法模式不能直接除**：GIMP 用 `safe_div()`（`|分子| ≤ 1e-6` 直接给 0，结果夹到 ±1e6），
  除零产生的超大值**不在中间夹**，而是留到写像素时统一夹 —— 颜色加深除零 → 0，颜色减淡除零 → 1。
- **「深色/浅色」是整像素模式**：比较的是双层**亮度** `Lum()`，然后整块取某一层的 RGB，
  不是逐通道取小/取大（逐通道那个是「变暗/变亮」）。
- **柔光是 GIMP/Pegtop 式**，不是 W3C 那条带 `sqrt` 的公式；PS 的柔光也不同实现，
  三者观感相近但数值不同。
- 分量组（色相/饱和度/颜色/明度）要先算整像素的 HSV/HSL 关系，不能逐通道独立处理。

### 3.2 Alpha 合成（GIMP `composite_union`）

```
αnew = αs + (1 − αs)·αb                      // αs = 像素 Alpha × 图层不透明度
ratio = αs / αnew
out   = ratio · [ αb·(comp − Cs) + Cs − Cb ] + Cb
```

展开就是常见写法（`B` = 上面那张表的 `comp`）：

```
out = ( αs·αb·B + αs(1−αb)·Cs + (1−αs)·αb·Cb ) / αnew
```

- 模式为**正常**时 `B = Cs`，退化成 Porter-Duff `over`。
- 这是一个 **并集（Union）** 合成：不做「只在上层范围内生效」（CLIP_TO_BACKDROP），
  所以透明背板上也能看见带模式的层 —— 与 PS 的默认观感一致。
- `αb = 0` 时公式自己就退化成 `out = Cs`。

## 4. 两种「透明度」

| 种类 | 含义 |
|------|------|
| **像素 Alpha** | 每个像素 RGBA 里的 A；软笔边缘、橡皮、透明层靠它 |
| **图层不透明度** | 面板滑条；合成时整层再乘：`αs = 像素A × 图层不透明度` |

新建透明层：逻辑全透（不是整层填 α=0，也没有瓦片）；白背景层：`fill(白)` 后各格为不透明白。
图层滑条不能代替「有的像素有色、有的全透」。详见 [tiles-and-memory.md](tiles-and-memory.md)。

**预乘**：存储时 RGB 已 × 像素 α（`Format_ARGB32_Premultiplied`）；混合计算前会先转回直通，
算完再预乘写回。与「图层不透明度滑条」不是同一件事；滑条是合成时额外乘一次。

## 5. 改一层后如何刷新

```
改图层（画 / 移 / 显隐 / 透明度 / 混合模式）
  → 更新文档数据（必要时 markDirty）
  → 合成器生成/更新合成图（全图或脏区）
  → 视图按当前缩放显示
```

- **朴素（本 Demo）**：从底到顶再合成（可限脏矩形）。混合模式是**合成期属性**，
  切模式不需要碰任何像素，只重合成一次。
- **优化（PS/GIMP）**：缓存下方已合成结果，动中间层时尽量只重算**该层及以上**。
  不是「只算上一层和下一层」——上面各层通常都要再盖；下面可靠缓存跳过。

**文档是真相**；合成图是中间结果；视图是显示器。不是改完只生成视图、跳过文档。

### 5.1 会话合成预览（自由变换）

自由变换拖中可在 `Layer` 上挂一张**文档坐标**的 `compositePreview`（非瓦片真相）。  
`Compositor` 对该层：先按平常叠瓦片（通常已挖空源区），再叠预览图；混合模式 / 不透明度 / 蒙版与该层一致。  
确认或取消时清掉覆盖。设计对照与改前写回本层的差异见 [../pending-dev.md](../pending-dev.md)。

## 6. 视图缩放 vs 合成

合成在**图像像素坐标**里完成；滚轮缩放只改变 `CanvasView` 如何把合成图画到窗口（本项目放大较多时关闭平滑插值，像素块更利落）。

## 7. 与 GIMP 的差异（有意，逐条可查）

| 差异 | GIMP | 本项目 | 影响 |
|---|---|---|---|
| 色彩空间 | 现代模式按 `gimp-layer-modes.c` 的 `blend_space` 逐模式选线性/感知空间，**并在线性空间合成** | 整条管线 8-bit sRGB 直通（与 PS 一致） | 数值不与 GIMP 逐位相同；观感对齐 PS |
| `comp` 夹取时机 | 不夹，留到输出时 | 同 | 半透明下不会算出「先夹后混」的偏差 |
| 溶解的随机源 | 固定种子表 + `GRand` | `(x,y)` 整数哈希 | 两者都可复现，点阵不同；本项目必须确定性，否则全量重合成会闪 |
| 亮度系数 | `babl_space_get_rgb_luminance`（sRGB） | 同系数（0.2126/0.7152/0.0722），在**编码值**上加权 | 深色/浅色/明度是近似 |
| 模式表 | 还有 `*_LEGACY`、LCh、颗粒合并等 | 只做 PS 的 27 种 | 打开 GIMP 的 XCF 不在范围内 |

> 公式对照见上表与 `engine/blend.cpp` 的 case 注释；与 GIMP 现代模式在色彩空间上
> 有意不同（见下节），数值不追求逐位一致。
