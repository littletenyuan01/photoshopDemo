# ROI（请求区 / 所需输入区）—— 为什么滤镜绕不开它

> 本文解释本项目滤镜引擎里那个贯穿一切的概念：**ROI**。
> 它是「按块合成 + 算子要读邻居」这两件事碰在一起的必然产物，也是「小块脏区只重算一点点」能成立的前提。
> 对照页：[operators.md](operators.md)（算子调度）、[../pending-dev.md](../pending-dev.md)（ROI 通用化的迁移设计）。

## 1. 一句话

> **ROI 通用化 = 让算子能回答「要算这块输出，我需要输入里的哪一块」，而且那块可以是任意矩形。**

现在本项目的算子只能回答「以请求区为中心、**对称**向外扩 p 圈」这一种形状；
GEGL/GIMP 里对应 `get_required_for_output()`，它返回一个**通用矩形**。

术语对照：

| 说法 | 含义 |
|---|---|
| **请求区**（out / want / ROI） | 这次要算出来的那块输出（典型是 64×64 的合成块，或画布可见区） |
| **所需输入区**（src / required input） | 为了算出请求区，必须准备好输入数据的那块 |
| 反向传播（back-propagation） | 从请求区推出所需输入区的过程 |

## 2. 为什么滤镜会牵扯到 ROI

因为**合成是分块的**（64×64），而滤镜**要读邻居**。两者一碰，就必须回答「算这一块，得喂哪一块」。

具体看**每个输出像素从哪里读输入**，算子分三类：

| 算子类型 | 输出像素读哪里 | 需要的输入 | 本项目现状 |
|---|---|---|---|
| **点算子**（亮度/对比度、反相） | 同一位置 `out(x,y) = f(in(x,y))` | 就是它自己 → pad = 0 | ✅ 天然没这个问题 |
| **邻域算子**（模糊、中值、3×3 锐化/浮雕/查找边缘） | 附近 `out(x,y) = f(in(x±r, y±r))` | 请求区**对称外扩 r 圈** | ✅ `inputPad()` 正好表达 |
| **几何算子**（位移、旋转、极坐标） | **别处** `out(x,y) = in(x-dx, y-dy)` | 一块**平移过的矩形** | ❌ 对称外扩表达不了 |

```
请求输出区 want（64×64）                邻域算子：要的是「外扩 r 圈」
┌────────┐                                    ┌──────────────┐
│  want  │  ← 算它，需要邻居                 │   srcWant    │
└────────┘                                    │  ┌────────┐  │
                                              │  │  want  │  │   ← 对称，好用 ✓
几何算子（位移 dx,dy）：要的是「平移过去的那块」│  └────────┘  │
        ┌──────────────┐                      └──────────────┘
        │   srcWant    │
        │      ┌───────┴──┐
        └──────┤  want    │                  ← 同样大小，但**偏在一边**：对称机制表达不了 ✗
               └──────────┘
```

### 为什么不能干脆「整层重算」

那就是本项目踩过的坑：64×64 的笔触却触发整层重算，实测 **447 ms** 卡顿。
ROI 反向传播就是让「小块脏区」只重算必要输入的那套机制，把 447 ms 压到 **8.9 ms**。
所以 ROI 不是滤镜的附加负担，而是**分块缓存能成立的前提**。

### 为什么答错会出可见 bug

所需输入区不够 → 64×64 块边界处邻居缺失 → 画面上出现**网格状接缝**。
这正是每个面积算子的「**分块投影 == 整幅合成，diffs=0**」断言守的东西（见 [../tests.md](../tests.md) 的 `areafilter`）。

## 3. 现状机制：只能对称外扩

| 位置 | 代码 |
|---|---|
| 标量外扩 | `FilterNode::inputPad()` → `int` |
| 滤镜栈求和 | `FilterStack::totalPad()` |
| 像素层路径 | `Layer::compositeRasterRegion(want)`：`srcWant = want.adjusted(-pad,-pad,pad,pad) ∩ 层范围`，之后**按 pad 取中心裁回** |
| 图路径 | `PointFilterOp::requiredInputs(out)` → `{ out.adjusted(-p,-p,p,p) }` |

## 4. 与 GIMP / GEGL 的对照

| | GIMP / GEGL | 本项目现状 |
|---|---|---|
| 输入需求的接口 | `get_required_for_output(operation, input_pad, output_roi) → **GeglRectangle**`（通用矩形） | `FilterNode::inputPad() → int`（对称标量） |
| 算子拿到什么 | `process` 里的 `roi` 是**输出矩形**，输入按需索取 | 拿到**整幅补边图**，就地处理 |
| 谁来裁剪 | 算子自己按映射写输出 | 调用方按 pad **取中心** |
| 每个节点的缓冲 | **每个节点各持一个 buffer**（请求式求值） | 一个滤镜栈共用一张补边图 |

参考实现：`gimp-master/app/operations/gimpoperationoffset.c` ——
`gimp_operation_offset_get_required_for_output()` 直接返回
`gimp_operation_offset_get_rect(offset, TRUE, output_roi, &rect)`，即**把输出矩形平移 (dx,dy)**。
（该算子还设了 `cache_policy = GEGL_CACHE_POLICY_NEVER`。）

用对称外扩去逼近位移的代价：`p = max(|dx|,|dy|)` ⇒ 每个 64×64 块要取 `(64+2p)²` 输入。
偏移 500px 时是 `(64+1000)² ≈ 113 万`像素，而真正需要的只有 `4096` —— **276 倍**无用功。
更麻烦的是 PS 默认的 **wrap-around**：它是**多矩形**依赖（区域在右边缘时要读最左边），单矩形机制无从表达。

## 5. 迁移设计（三步，每步可单独验证）

| 步骤 | 内容 | 验收标准 |
|---|---|---|
| ① 契约带几何 | `FilterContext` 增 `QRect srcRect; QRect outRect;`（层/文档坐标）；两条路径填真值；`FilterEval::applyNode(image, node)` 直调路径保持「src = out = 图像自身」语义 | **七个面积算子行为完全不变 ⇒ 全套测试保持绿** |
| ② 裁剪按几何 | `compositeRasterRegion` 改用 `FilterStack::requiredInputRect(want)`；裁剪点由「按 pad 取中心」改为 `want.topLeft() - srcWant.topLeft()` | 测试仍绿 |
| ③ 算子契约改 `process(const QImage &src, QImage &out, ctx)` | 算子从 ctx 读映射、自己写输出 | 破坏性：七处内环取样基准都要改 |

### 多算子栈的额外门槛（别低估）

栈里若含非 pad 类算子（如 offset），**每个算子都需要各自的中间缓冲与几何**，
不能像现在这样「所有滤镜依次作用在同一张补边图上」：

```
现状（共用几何）:  srcWant ──[模糊]──[USM]──[锐化]──► 裁回 want
含几何算子      :  srcWant ──[模糊]──► 中间图A ──[位移]──► 中间图B ──► 裁回 want
                                      ↑ 几何不同，得各持一个 buffer
```

这正是 GEGL 让**每个节点各持一个 buffer** 的根本原因。
⇒ 补第 ③ 步之前先决策：**是否接受「一个栈里最多一个几何类算子、且必须在最后」**这种受限语义
（可以先上 offset，代价是记一条明确限制）。

## 6. 已落地的一条相关实践

滤镜对话框的**区域化预览**（`ImageDocument::setLayerFilterNodePreview(..., region)`）就是 ROI 思想在 UI 上的应用：
只把**画布可见区**标脏，屏幕外的旧像素不动（对话框是模态的，视口不会变），因此拖动参数不会整层重算。
见 [../pending-dev.md](../pending-dev.md) 的「滤镜预览的两道护栏」。
