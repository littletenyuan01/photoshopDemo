# 钢笔工具 + 路径系统 —— 开发计划（详版 · 交付 Cursor）

> 参考：GIMP（`gimp-master/gimp-master/app/path/*`、`app/tools/gimppathtool.c`、`app/display/gimptoolpath.c`）
> 交互与观感对齐 Photoshop 钢笔。
>
> **本文件是施工图**：每个文件、每个函数签名、每步验收断言都写死，便于逐条实现与核对。
>
> **已定范围（用户确认）**
> 1. ✅ 做钢笔 + 路径数据 + 选区互转；**不碰矢量蒙版**
> 2. ✅ 填充规则**对齐 PS = non-zero（WindingFill）**
> 3. ✅ **要实现真正的弯度钢笔算法**（不得回退到普通钢笔行为）

---

## 0. 工程约定（先读，否则会踩本项目已有的坑）

| 约定 | 出处 | 说明 |
|---|---|---|
| 新增 `OpName` **必须加在 `Count` 之前** | `gpu-plan.md §9 坑 5` | `.pslite` 存的是枚举整数值，插在中间会让旧文件错位 |
| 新增 `OpName` **必须登记成本表** | `tests/t_rasterbackend.cpp` | 否则「成本表覆盖全部 OpName」用例会红 |
| 改 `app.pri` / `.pro` 后**必须重跑 qmake** | `gpu-plan.md §9 坑 1` | 否则 `undefined reference` |
| **不要**用 PowerShell 来回读写含中文的文件 | `gpu-plan.md §9 坑 4` | `Get-Content\|Set-Content` 会毁编码；用编辑器或 `[System.IO.File]::WriteAllText` + UTF8 无 BOM |
| 关键算法必须写**中文注释**，且写清「为什么这么写」 | `.cursor/rules/code-comments.mdc` | 不是「做了什么」，是「为什么」 |
| 路径栅格化**走 CPU，不写 GPU** | `gpu-plan.md §4.4.1` | 逐扫描线求交是数据相关分支，不适合当前 compute-shader 后端 |
| `tools/` 层**不得**包含任何 UI 头文件 | `tools/tool.h` 注释 | 保持 tools 不依赖 ui |

### 构建与验证命令（照抄）

```bat
:: 工具链
::   qmake  D:\Qt\6.11.0\mingw_64\bin\qmake.exe
::   make   D:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe

cd <repo>\photoshopDemo

:: 测试构建
qmake psDemo\tests.pro -o .build-tests\Makefile
cd .build-tests && mingw32-make -f Makefile.Release -j8

:: 跑用例（必须设 TEMP，否则 .pslite 往返类用例会假红）
set QT_QPA_PLATFORM=offscreen
set TEMP=<repo>\photoshopDemo\.build-verify\tmp
release\psdemo_tests.exe                    :: 全部，退出码 = 失败数
release\psdemo_tests.exe pathgeom           :: 只跑本阶段新用例

:: 主程序
qmake psDemo\psDemo.pro -o .build-app\Makefile
cd .build-app && mingw32-make -f Makefile.Release -j8
```

**基线**：动手前先跑一次全套，应为 **36/36 通过**。每阶段结束必须 ≥ 36+N 全绿。

---

## 1. 现状盘点

### 1.1 已有，**不要重做**

| 设施 | 位置 | 怎么用 |
|---|---|---|
| 6 个钢笔工具 ID | `tools/toolid.h` | `Pen` / `FreeformPen` / `CurvaturePen` / `AddAnchorPoint` / `DeleteAnchorPoint` / `ConvertPoint` |
| 工具选项页 | `ui/tooloptionsbar.ui` 的 `pagePath` | 已建好且**已接线**；含「多边形 / 自动添加删除 / 橡皮带 / 填充路径 / 描边路径 / 线宽」 |
| 选项页上的控件名 | 同上 | `pathModeCombo` `pathPolygonalCheck` `pathAutoAddCheck` `pathRubberBandCheck` `pathFillCheck` `pathStrokeCheck` `pathStrokeWidthEdit` |
| 工具→选项页映射 | `ui/tooloptionsbar.cpp:350-356` | 三个钢笔 ID 已映射到 `pagePath`，**无需改** |
| 工具图标 | `resources/icons/tools/` | `pen.png` `freeform-pen.png` `curvature-pen.png` |
| 路径树底栏图标 | `resources/icons/paths/` | `new-path.svg` `from-selection.svg` `to-selection.svg` `stroke.svg` |
| 画布浮层机制 | `ui/canvasview.cpp:623-637` | 通用：实现 `hasOverlay()`/`drawOverlay()` 即可，**不改 CanvasView** |
| 事件模型 | `tools/toolevent.h` | `imagePos`(图像) + `widgetPos`(控件) + `modifiers` + `doubleClick` |
| 视图变换 | `tools/toolcontext.h` | `imageToWidget(pos)`、`viewZoom`、`viewOffset` |
| 双击已处理 | `ui/canvasview.cpp:746` | Qt 双击不发 `MousePress`，这里已转发 |
| 撤销框架 | `app/undoitem.h` | `UndoItem{name/byteSize/pop}` + `SelectionUndo` 可照抄 |
| 选区合并 | `domain/selection.h:114` | **`combineShapeMask(QImage, ChannelOp, QRect)`** = 路径→选区落点 |
| 形状栅格化参照 | `engine/op/shapefillop.cpp` | 灰 mask 栅格化模式，路径栅格化照它写 |
| 工具预览/提交分离参照 | `tools/transformtool.cpp` | 拖中 `compositePreview`，松手提交 |

### 1.2 缺，**要做**

1. `domain/` 无任何路径模型（`ui/pathtreepanel.h` 自注"尚无 path domain，仅 UI 骨架"）
2. 全仓库无贝塞尔代码（搜 `Bezier` 零命中）
3. `tools/toolmanager.cpp:39-67` **6 个钢笔 ID 一个都没注册** ⇒ 点了回退中性工具（无反应）

### 1.3 GIMP 参考规模（工时锚点）

| 文件 | 行数 |
|---|---|
| `app/path/gimpbezierstroke.c` | 2621 |
| `app/display/gimptoolpath.c` | 2121 |
| `app/path/gimppath-import.c` | 1938（我们不做） |
| `app/path/gimppath.c` | 1492 |
| `app/tools/gimppathtool.c` | 1196 |
| `app/display/gimpcanvaspath.c` | 355 |
| `app/path/gimppath-export.c` | 344 |
| `app/path/gimppathundo.c` | 228 |
| `app/path/gimppath-warp.c` | 210 |
| `app/core/gimpbezierdesc.c` | 191 |
| `app/actions/tool-path-commands.c` | 94 |
| **合计** | **≈10826** |

本计划目标 **≈2200–2800 行 C++**（不含 SVG 导入导出）。

---

## 2. 关键技术决策（已定，实现时不要偏离）

### 决策 A：数据结构照 PS，**不照抄 GIMP 的 anchor/control 分离**

GIMP 把锚点与控制柄都存成 `GimpAnchor`、靠 `type` 区分
（`app/path/path-enums.h:24-28`：`GIMP_ANCHOR_ANCHOR` / `GIMP_ANCHOR_CONTROL`），
因为它的 `GimpStrokeClass` 要支持多种 stroke 实现（30+ 虚函数）。
**本仓库无此需求**，照抄等于白背 30 个虚函数。

**采用**：一个锚点自带 in/out 两个控制柄，**存绝对文档坐标**
（存相对偏移在"移动整条路径/多选拖动"时会累积浮点误差；PS 的 `.psd` 也是绝对坐标）。

### 决策 B：曲线段一律三次贝塞尔；直线 = 控制柄退化到端点

这样只需**一套**求值/细分/命中/栅格化代码。
GIMP 的 `lineto`/`conicto`/`cubicto` 三套（`gimpbezierstroke.h:55-63`）是为解析 SVG `d`，我们不做。

### 决策 C：撤销用整条路径快照，不做增量

GIMP 的 `gimppathundo.c` 仅 228 行 ⇒ 它也是粗粒度。
一条路径几百锚点 ≈ 几 KB，`UndoItem::byteSize()` 扛得住。
增量撤销（"哪个锚点动了多少"）会引入"撤销后索引失效"这类难查 bug，**不值得**。

### 决策 D：**填充规则 = non-zero（WindingFill），对齐 PS**

- **不要**自己写扫描线求交来做填充规则。**用 `QPainterPath`**：
  ```cpp
  QPainterPath pp;
  pp.setFillRule(Qt::WindingFill);   // ← PS 口径（Qt 默认是 OddEvenFill）
  // 逐子路径 moveTo/cubicTo…
  QImage mask(size, QImage::Format_Grayscale8);
  mask.fill(0);
  QPainter p(&mask);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::white);
  p.drawPath(pp);                     // 抗锯齿覆盖率由 Qt 光栅引擎给出
  ```
- 子路径**方向**决定挖洞（外顺内逆 ⇒ 洞）。这是 non-zero 的语义，**必须在用例里钉住**。
- 为什么用 `QPainter` 而不是手写求交：Qt 已经做了抗锯齿覆盖率与 WindingFill，
  手写会同时丢掉 AA 与正确性，且要自己处理自交路径。

### 决策 E：**弯度钢笔（CurvaturePen）用插值样条，控制柄可被手动覆盖**

**算法（必须按此实现）**：对锚点序列做 **Catmull-Rom 样条 → 三次贝塞尔**转换。

对相邻三点 `P[i-1], P[i], P[i+1]`，段 `P[i] → P[i+1]` 的贝塞尔控制柄：

```
tangent_i     = (P[i+1] - P[i-1]) * 0.5        // 中心差分切线
tangent_{i+1} = (P[i+2] - P[i])   * 0.5

outHandle[i]   = P[i]   + tangent_i     / 3 * tension
inHandle[i+1]  = P[i+1] - tangent_{i+1} / 3 * tension
```

- `tension` 初值 **1.0**（标准 Catmull-Rom）；**这是一个待目测调参的常数**，
  交付时把它提成 `CurvaturePen` 的具名常量并在注释里写明"对齐 PS 观感用的可调参数"。
- **端点处理（开路径）**：`P[0]` 用 `tangent_0 = P[1] - P[0]`；
  末点 `P[n-1]` 用 `tangent = P[n-1] - P[n-2]`（单侧差分）。
- **闭合路径**：点序列首尾相连，索引取模（`P[-1]` = 最后一点，`P[n]` = 第一点）。

**与手动编辑共存**（PS 行为：弯度钢笔也能拖出控制柄覆盖自动结果）：

`PathAnchor` 增加**显式标志**：

```cpp
bool inExplicit  = false;   // true = 用户手动拖过 / 指定过，自动推导不许覆盖
bool outExplicit = false;
```

- 自动推导**只写** `!explicit` 的那一侧。
- 用户拖控制柄 ⇒ 把该侧置 `explicit = true`（并镜像另一侧，见 §4.4 修饰键表）。
- 提供 `recomputeAutoHandles(subPathIdx)`：把所有非 explicit 的控制柄按上式重算；
  在「增/删锚点、移动锚点」后调用。

> ⚠️ **诚实标注**：这是"插值 C¹ 样条"的**可验证**实现，观感对齐 PS 弯度钢笔，
> **不是**逐点复刻 Adobe 私有算法。把这句话写进 `docs/features.md` 的限制节。

---

## 3. P0 —— 路径数据模型与贝塞尔数学（无 UI，先做）

### 3.1 新增 `domain/path.h`

```cpp
/**
 * path.h — 路径数据模型（domain 层）。
 *
 * 【对照 GIMP】app/path/gimppath.c 的 GimpPath + GimpStroke。
 * 【刻意不照抄】GIMP 把锚点与控制柄都存成 GimpAnchor、靠 type 区分，
 * 是为了支持多种 stroke 实现（GimpStrokeClass 有 30+ 虚函数）。本仓库没有多态
 * stroke 的需求，照抄等于白背 30 个虚函数 —— 故采用 PS 口径：一个锚点自带
 * in/out 两个控制柄，且**存绝对文档坐标**（存相对偏移在"移动整条路径/多选拖动"
 * 时会累积浮点误差；PS 的 .psd 里也是绝对坐标）。
 */
#ifndef DOMAIN_PATH_H
#define DOMAIN_PATH_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace Ps {

/// 单个锚点（PS 口径：自带前后控制柄，绝对文档坐标）。
struct PathAnchor
{
    QPointF point;          ///< 锚点位置（文档坐标）
    QPointF inHandle;       ///< 进入控制柄（绝对坐标；仅 inExplicit 或已推导时有意义）
    QPointF outHandle;      ///< 离开控制柄（绝对坐标）
    /**
     * 是否由用户显式指定（拖过控制柄）。
     * 【为什么需要】弯度钢笔会自动推导控制柄；一旦用户手动拖过，自动推导就不许再覆盖它。
     * 这是"自动曲线 + 手动微调可共存"的唯一开关。
     */
    bool inExplicit = false;
    bool outExplicit = false;
    /// 尖角（两侧控制柄各自独立）；false = 平滑（共线）
    bool corner = false;
    bool selected = false;  ///< 供多选拖动

    bool hasIn() const { return inExplicit || inHandle != point; }
    bool hasOut() const { return outExplicit || outHandle != point; }
};

/// 一条子路径（对照 GIMP 的一个 GimpStroke）。
struct SubPath
{
    QVector<PathAnchor> anchors;   ///< 顺序即描画顺序
    bool closed = false;

    bool isEmpty() const { return anchors.size() < 2; }
    int segmentCount() const;      ///< closed ? anchors.size() : anchors.size()-1
};

/// 一条命名路径（对照 GimpPath：一条路径可含多个子轮廓；PS 叫"路径"里的多条子路径）。
class Path
{
public:
    QString name;
    QVector<SubPath> subPaths;
    bool visible = true;          ///< 路径树里的眼睛

    // —— 查询 ——
    QRectF bounds() const;
    bool isEmpty() const;
    int totalAnchors() const;

    // —— 锚点编辑（对照 gimp_stroke_anchor_*）——
    /// 在 (sub, seg) 段上按参数 t∈(0,1) 插入锚点；用 de Casteljau 分裂，形状不变。
    bool insertAnchor(int sub, int seg, qreal t);
    /// 删除锚点；保持端点不动，曲线尽量贴合（把两侧控制柄按比例外推）。
    bool deleteAnchor(int sub, int anchorIdx);
    /// 移动锚点：连带平移两个控制柄（保持相对形状）。
    bool moveAnchor(int sub, int anchorIdx, const QPointF &newPoint);
    /// 尖角 ↔ 平滑切换（对照 gimp_stroke_anchor_convert）。
    bool setCorner(int sub, int anchorIdx, bool corner);
    /// 把某侧控制柄设为显式（用户拖过）并可选镜像另一侧。
    bool setHandle(int sub, int anchorIdx, bool isOut, const QPointF &h, bool mirror);
    /// 弯度/自动曲线专用：按 Catmull-Rom 重算所有非 explicit 的控制柄。
    void recomputeAutoHandles(int sub, qreal tension);

    // —— 命中（容差单位 = **控件像素**，由调用方换算，见 §4.3）——
    struct Hit { int sub = -1; int index = -1; qreal dist = 0.0; };
    Hit hitTestAnchor(const QPointF &docPos, qreal tolDoc) const;
    struct HandleHit { int sub = -1; int index = -1; bool isOut = false; qreal dist = 0.0; };
    HandleHit hitTestHandle(const QPointF &docPos, qreal tolDoc,
                            bool selectedOnly = true) const;
    /// 最近点在线段上（含 t 与段序号）；miss 时返回 sub=-1。
    struct SegHit { int sub = -1; int seg = -1; qreal t = 0.0; qreal dist = 0.0; };
    SegHit nearestOnPath(const QPointF &docPos, qreal tolDoc) const;

    // —— 变换 ——
    void translate(const QPointF &delta);
    void transform(const QTransform &m);

    // —— 与 QPainterPath 互转（栅格化/命中复用 Qt）——
    QPainterPath toPainterPath() const;
    static Path fromPainterPath(const QPainterPath &pp);

    // —— 序列化（.pslite；见 P3）——
    void write(QDataStream &out) const;
    static Path read(QDataStream &in);
};

} // namespace Ps
#endif
```

### 3.2 新增 `engine/bezier.h/.cpp`（纯函数，无状态，**最容易被单测覆盖**）

```cpp
namespace Ps::Bezier {

struct Seg { QPointF p0, p1, p2, p3; };   // 三次贝塞尔：p0/p3 端锚点，p1/p2 控制柄

/// 由两个锚点构造段（控制柄取自 anchor.outHandle / next.inHandle）。
Seg fromAnchors(const PathAnchor &a, const PathAnchor &b);

QPointF pointAt(const Seg &s, qreal t);
QPointF tangentAt(const Seg &s, qreal t);          ///< 未归一化的导数；调用方自行 normalize
/// de Casteljau 分裂：返回 {左段, 右段}，且 左段.p3 == 右段.p0 == pointAt(t)
QPair<Seg, Seg> splitAt(const Seg &s, qreal t);
/// 控制点凸包（**不需精确极值**：凸包足够给脏区用，且不会漏）
QRectF bounds(const Seg &s);
/// 折线近似；tol = 最大偏离（文档像素）
QVector<QPointF> flatten(const Seg &s, qreal tol, int *stepsOut = nullptr);
qreal length(const Seg &s, int steps = 32);
/// 按弧长参数取点（描边等距落 dab 用）
QPointF pointAtLength(const Seg &s, qreal len, int steps = 32);

} // namespace Ps::Bezier
```

**实现要点**
- `splitAt` 用标准 de Casteljau：`p01=(p0+p1)/2, p12=(p1+p2)/2, p23=(p2+p3)/2, p012=(p01+p12)/2, p123=(p12+p23)/2, p=(p012+p123)/2`，
  左段 `{p0,p01,p012,p}`，右段 `{p,p123,p23,p3}`。
- `flatten` 的细分判据：控制点到弦的距离 < tol 即停（**不要**固定步数，
  固定步数在长段上会糊、在短段上会白算）。
- `bounds` 用 `p0..p3` 的 min/max（凸包），**故意不用精确极值**：
  精确极值要解一元二次并处理分母为 0，收益只是脏区小一点，不值得。

### 3.3 新增常驻用例 `psDemo/tests/t_pathgeom.cpp`

**这是 P0 的验收，必须全部通过才算完。**每条都写进 `Reporter::check`。

| # | 断言 | 为什么这条 |
|---|---|---|
| 1 | `splitAt(t)` 后两段各自在 `0..1` 采样，与原段在 `[0,t]`/`[t,1]` 对应参数处**逐点 < 1e-9** | 插入锚点"形状不变"的数学根据 |
| 2 | 上条对 `t ∈ {0.1, 0.25, 0.5, 0.75, 0.9}` 全覆盖 | 防只测中点 |
| 3 | `insertAnchor` 后 `flatten` 点集与插入前**逐点 < 1e-6** | 形状不变是这条的语义核心 |
| 4 | `deleteAnchor` 后**两端锚点坐标不变**，中间偏移 < 容差 | 删除不许把端点带跑 |
| 5 | `bounds()` 包住 `flatten()` 的所有点 | 脏区不许漏 |
| 6 | `moveAnchor` 后两个控制柄相对锚点的偏移**不变** | 平滑性不被破坏 |
| 7 | `hitTestAnchor`：tol 内命中、tol*1.1 外不命中（**边界钉死**） | 命中容差易写松 |
| 8 | `hitTestHandle`：只命中 `selected` 锚点的控制柄 | PS：控制柄只在选中锚点时可见可拖 |
| 9 | `nearestOnPath` 返回的 `t` 与手工采样最近点一致 | 线段命中 |
| 10 | `recomputeAutoHandles`：三点共线时控制柄落在连线上（切线正确） | 弯度算法基本正确性 |
| 11 | `recomputeAutoHandles`：**不覆盖** `explicit` 的那一侧 | 决策 E 的核心 |
| 12 | `toPainterPath` 往返 `fromPainterPath` 后锚点数与控制柄误差 < 1e-9 | 与 Qt 互转无损耗 |
| 13 | 闭合子路径 `segmentCount()` == 锚点数；开路径 == 锚点数-1 | 段数口径 |

**登记**：`psDemo/tests.pro` 加 `tests/t_pathgeom.cpp`；`tests/testmain.cpp` 加
`{ "pathgeom", "路径几何：贝塞尔分裂/扁平化/命中/自动曲线控制柄", tPathGeom }`。

> ⚠️ 改完 `tests.pro` **必须重跑 qmake**，否则新文件不参与编译（本项目坑 1）。

---

## 4. P1 —— 栅格化：路径 → 选区 / 填充 / 描边

### 4.1 新增 `engine/op/pathfillop.h/.cpp`

**职责**：把路径栅格化成 `Format_Grayscale8` 覆盖率 mask（0..255），交给调用方合并进选区或直接绘制。

```cpp
namespace Ps {
/**
 * 路径栅格化算子（对照 GIMP gimp_path_to_selection 的落地）。
 *
 * 【为什么用 QPainterPath 而不是手写扫描线求交】
 * PS 口径是 **non-zero 环绕规则**（Qt: Qt::WindingFill），且需要抗锯齿。
 * Qt 的光栅引擎两样都已做对；手写会同时丢掉 AA 与自交路径的正确性。
 * 输入图约 1.9 Mpx 时实测 drawPath 在毫秒级，足够。
 */
class PathFillOp
{
public:
    /// 产出灰 mask；rect = 要覆盖的区域（通常是路径 bounds ∩ 文档）
    static QImage rasterize(const Path &path, const QSize &docSize,
                            Qt::FillRule rule = Qt::WindingFill);
};
} // namespace Ps
```

**实现**（照 `engine/op/shapefillop.cpp` 的结构）
1. `path.toPainterPath()`，`setFillRule(WindingFill)`
2. `QImage mask(docSize, Format_Grayscale8)`，`fill(0)`
3. `QPainter` + `Antialiasing` + `NoPen` + `white brush` + `drawPath`
4. 返回 mask

### 4.2 新增 `engine/op/pathstrokeop.h/.cpp`

**职责**：沿路径等距落 dab 描边（复用现有 dab 体系，观感与画笔一致）。

```cpp
class PathStrokeOp
{
public:
    /**
     * 沿路径描边。
     * @param halfWidth  dab 半径（= 选项栏 pathStrokeWidthEdit / 2）
     * @param spacing    相邻 dab 间距系数（0.25 ≈ PS 默认手感）
     */
    static void stroke(const Path &path, PaintEngine &engine, const QColor &color,
                       qreal halfWidth, qreal spacing = 0.25);
};
```

**实现**：`bezier.flatten` 得折线 → 按弧长累加，到 `spacing * 2 * halfWidth` 落一个 dab
→ 调 `PaintEngine`（与 `tools/painttool.cpp` 同一条 dab 路径）。
**不要**用 `QPainter::strokePath`：那会得到一条纯色描边，与画笔 dab 观感不一致。

### 4.3 ⚠️ **不要**为路径栅格化新增 `OpName`

`PathFillOp` / `PathStrokeOp` 是**静态工具类**（由钢笔 / 路径树直接调用），**不进** `FilterStack`、**不进**调整层。

| 若错误地加在 `BrightnessContrast..Count` 段 | 后果 |
|---|---|
| `isAdjustmentOp` 默认 true | 可被 `addAdjustmentLayer` 接受；属性页无对应页 |
| 成本表要登记但从不出现在滤镜链 | 语义噪音 |

**若将来必须进枚举**：只能放在 **`ShapeFill` 旁的绘制段**，并**显式排除**出 `isAdjustmentOp`；本计划 **P0–P4 不做**。

### 4.4 接入选区

**不要新写选区合并逻辑**，用现成的：

```cpp
const QImage mask = PathFillOp::rasterize(path, doc->size());
doc->selection().combineShapeMask(mask, op, path.bounds().toAlignedRect());
```

`ChannelOp` 由修饰键决定（与 `marqueeselecttool.cpp` 的 `opFromModifiers` 同一口径）。

### 4.5 新增常驻用例 `psDemo/tests/t_pathraster.cpp`

| # | 断言 | 备注 |
|---|---|---|
| 1 | 矩形路径填充（WindingFill）== `Selection::selectRectangle` 结果，**AA 边界逐像素差 ≤ 1** | 内部必须完全一致 |
| 2 | 同心两子路径：**外顺内逆 ⇒ 中心是洞**（中心像素 mask==0） | **non-zero 的核心语义** |
| 3 | 同上但**外逆内顺 ⇒ 中心也是洞**（non-zero 只看方向差） | 与 even-odd 的区别在这里 |
| 4 | **同向**嵌套两子路径 ⇒ 中心**不是**洞（non-zero 语义） | even-odd 下这里会是洞 ⇒ 这条专抓"误用 OddEvenFill" |
| 5 | 自交"8 字形"路径：两个环都为实心（non-zero） | 抓 OddEvenFill 误用 |
| 6 | 描边直线路径：dab 中心落在线上、间距符合 `spacing` | |
| 7 | 路径→选区→`clear()`→再合并，结果稳定（可重入） | |
| 8 | 空路径/单点路径不崩、产出全 0 mask | 边界 |

**第 3、4、5 条是本次决策 D 的护栏**：如果实现里误用 `OddEvenFill`，第 4、5 条必红。

---

## 5. P2 —— 钢笔工具交互（本阶段交付"能用的钢笔"）

### 5.1 新增 `tools/pentool.h/.cpp`

```cpp
/**
 * pentool.h — 钢笔工具（tools 层）。
 *
 * 【对照 GIMP】app/tools/gimppathtool.c 的状态机 + app/display/gimptoolpath.c 的命中/绘制。
 * 【对齐 PS】点空白落点、拖出控制柄、点线段插点、Ctrl 点删点、Alt 点转换、Enter 结束。
 *
 * 【本工具不写文档】拖动中只改工具内的 m_edit（临时副本）；mouseRelease 才提交
 * 一次并压一条撤销 —— 与 transformtool 的 preview/commit 分离同一套路。
 * 拖中每帧写文档会让每个 mousemove 都进一次撤销栈，这是本项目已经吃过一次的坑
 * （见 docs/pending-dev.md 台账 #20 那一类"公式只在某种调用形态下才对"）。
 */
class PenTool : public Tool
{
    Q_OBJECT
public:
    explicit PenTool(Ps::ToolId id, QObject *parent = nullptr);

    Qt::CursorShape cursorShape() const override;   // Qt::CrossCursor
    bool hasOverlay() const override { return true; }
    void drawOverlay(QPainter &painter, const ToolContext &ctx) const override;

    bool mousePress(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool mouseMove(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool mouseRelease(const ToolEvent &e, const ToolContext &ctx, ViewPort &view) override;
    bool keyPress(int key, Qt::KeyboardModifiers mods,
                  const ToolContext &ctx, ViewPort &view) override;
    bool wantsShortcutOverride(int key, Qt::KeyboardModifiers mods) const override;
    void deactivate(const ToolContext &ctx, ViewPort &view) override;

private:
    /// GIMP: GimpPathToolState 的裁剪版
    enum class State { Idle, PlacingAnchor, DraggingOutHandle, DraggingAnchor, DraggingHandle };
    enum class Target { None, Anchor, HandleIn, HandleOut, Segment, Empty };

    Target classify(const ToolEvent &e, const ToolContext &ctx) const;
    qreal tolDoc(const ToolContext &ctx) const;   ///< 控件容差 → 文档容差
    void commitToDocument(const ToolContext &ctx);

    State m_state = State::Idle;
    bool m_isCurvature = false;     ///< ToolId::CurvaturePen
    bool m_isFreeform = false;      ///< ToolId::FreeformPen
    Path m_edit;                    ///< 正在编辑的临时路径
    bool m_hasEdit = false;
    int m_activeSub = -1, m_activeIdx = -1;
    bool m_activeIsOut = false;
    QPointF m_dragStart;            ///< 用于算 delta
};
```

### 5.2 状态机（**照此实现**）

```
Idle
 ├─ 左键按空白 ─────────────→ 追加锚点；State=DraggingOutHandle
 │                              （自由钢笔：改为按下即开始自由采样，见 5.5）
 ├─ 左键按锚点 ─────────────→ 选中；State=DraggingAnchor
 ├─ 左键按控制柄(仅选中锚点) → State=DraggingHandle
 ├─ 左键按线段 ─────────────→ if (pathAutoAddCheck) 该处 insertAnchor，State=DraggingAnchor
 ├─ Ctrl+左键按锚点/线段 ───→ if (pathAutoAddCheck) deleteAnchor（立即，无拖动）
 ├─ Alt+左键按锚点 ─────────→ corner 取反（尖角↔平滑）
 ├─ 双击 ──────────────────→ 结束（open）
 └─ Enter ─────────────────→ 结束（closed）

DraggingOutHandle / DraggingAnchor / DraggingHandle
 └─ 左键松开 ──────────────→ 提交一次 + 压一条撤销；State=Idle

Esc（任意状态）→ 丢弃 m_edit，不留任何痕迹（**不压撤销**）
```

### 5.3 拖动行为

| 拖什么 | 效果 |
|---|---|
| 空白落下后拖 | 该锚点 `outHandle` = 光标；`inHandle` = 锚点的镜像（PS：按下即拖出对称控制柄） |
| 拖锚点 | 锚点+两个控制柄整体平移（`Path::moveAnchor`） |
| 拖控制柄 | 改该侧；**默认镜像另一侧保持平滑**（除非 Alt，见 5.4） |
| 拖线段 | 该段两端锚点整体平移 |

### 5.4 修饰键总表（**P2 必须全部实现**）

| 键 | 行为 | 对应 GIMP |
|---|---|---|
| 裸拖控制柄 | 镜像另一侧、**长度可变**（共线） | `GIMP_ANCHOR_FEATURE_ALIGNED` |
| `Alt` + 拖控制柄 | **打断对称**，只动这一侧（成尖角） | `GIMP_ANCHOR_FEATURE_NONE` |
| `Shift` + 拖 | 约束到 45° 步进 | —— |
| `Ctrl` + 点击锚点/线段 | 删除 | 对应 DeleteAnchorPoint 工具 |
| `Alt` + 点击锚点 | 尖角 ↔ 平滑切换 | 对应 ConvertPoint 工具 |
| `Esc` | 取消当前路径（不留痕） | —— |
| `Enter` | 结束并闭合 | `gimp_path_tool_...` 结束路径 |

> `path-enums.h:30-36` 的 `FEATURE_NONE/EDGE/ALIGNED/SYMMETRIC` 就是这套联动的 GIMP 原名；
> 注释里带上这个对照，便于后续查证。

### 5.5 自由钢笔（`FreeformPen`）与弯度钢笔（`CurvaturePen`）

- **自由钢笔**：按下 → 移动中按**距离阈值**（建议 3 文档像素）追加锚点 → 松开结束；
  追加后调 `recomputeAutoHandles`（得到平滑曲线而不是折线）。
- **弯度钢笔**：只做"点击落点"，**每次落点后都调 `recomputeAutoHandles(m_activeSub, kTension)`**；
  点已有锚点/线段的行为与普通钢笔一致（PS 也允许编辑）。默认**不拖控制柄**
  （拖了就按 5.4 变显式）。
- 两者都必须走 **P0 的 `recomputeAutoHandles`**，不得各写一份切线公式。

### 5.6 浮层绘制（`drawOverlay`）

**照 `tools/polygonallassotool.cpp` 的 `drawOverlay` 写法**。要点：

| 元素 | 尺寸（**控件像素**） | 颜色 |
|---|---|---|
| 路径本体 | 1.0 | 前景色或白色（按背景明度） |
| 锚点方块 | 7×7 | 白填充+黑边；`selected` 用 `#0d66d0` 填充 |
| 控制柄圆点 | 5 直径 | 空心 |
| 控制柄连线 | 1.0 | 细虚线 |
| 橡皮带（`pathRubberBandCheck`） | 1.0 虚线 | 从末锚点到当前光标 |

**命中与绘制必须用同一套尺寸常量**（定义成文件内 `constexpr`），
否则会出现"看得见却点不中"。

### 5.7 注册（`tools/toolmanager.cpp`）与选项栏

在 `registerTool(...)` 列表里加：

```cpp
registerTool(std::make_unique<PenTool>(Ps::ToolId::Pen));
registerTool(std::make_unique<PenTool>(Ps::ToolId::FreeformPen));
registerTool(std::make_unique<PenTool>(Ps::ToolId::CurvaturePen));
```

> `AddAnchorPoint` / `DeleteAnchorPoint` / `ConvertPoint` 留到 P4（共用实现）。

**`pathModeCombo`（路径 / 形状）**：本计划**不做形状层 / 矢量蒙版**。
P2 起将该 combo **固定为「路径」并禁用「形状」项**（或整控件灰显），避免点了悬空。
形状模式单列后续任务，**不要**在 P2–P4「顺手做」。

### 5.8 新增常驻用例 `psDemo/tests/t_pentool.cpp`

**直接用 `ToolManager` 喂 `ToolEvent`**，不需要真窗口（照 `t_filterchain` 那种直调法）。

| # | 断言 |
|---|---|
| 1 | 点 3 下 + Enter ⇒ 文档多 1 条路径、1 个子路径、3 个锚点、`closed == true` |
| 2 | 点 3 下 + Esc ⇒ **路径数不变**、**撤销栈深度不变** |
| 3 | 点 2 下 + 拖出控制柄 ⇒ 控制柄非退化（`outHandle != point`）且 `inHandle` 镜像 |
| 4 | 拖控制柄后**两端锚点坐标不变** |
| 5 | `Alt`+拖控制柄 ⇒ 另一侧坐标**不变**（与第 3 条对比） |
| 6 | 拖动过程中 `pathsChanged()` **不发射**；松手后**恰好发射一次** |
| 7 | 一次拖动只压**一条**撤销；撤销后回到拖动前状态 |
| 8 | `Ctrl`+点锚点 ⇒ 锚点数 -1；再撤销 ⇒ 回到 -1 之前 |
| 9 | `Alt`+点锚点 ⇒ `corner` 取反 |
| 10 | 线段上点击（`pathAutoAddCheck` 开）⇒ 锚点数 +1 且**曲线形状不变**（`flatten` 采样 < 1e-6） |
| 11 | 弯度钢笔：点 4 点后 `recomputeAutoHandles` 已生效 ⇒ 相邻段切线在锚点处**共线**（夹角 < 1e-6） |
| 12 | 弯度钢笔：手动拖过某锚点的 out 控制柄后，再点新点触发重算 ⇒ 该控制柄**不被覆盖** |
| 13 | `AddAnchorPoint` 等三个 ID **此阶段未注册** ⇒ 选中它们走中性工具（不崩） |

**第 11、12 条是决策 E 的护栏。**

---

## 6. P3 —— 路径树接线 + 撤销 + 持久化

### 6.1 `domain/imagedocument.*` 加路径

- 加 `PathStack m_paths`（照 `LayerStack`：`std::vector<std::unique_ptr<Path>>`）
- 加访问器 `const PathStack &paths() const`、`Path *activePath()`
- 加**语义化 setter**（照现有 `setLayerVisible` 的收口风格）：
  `addPath` / `removePath` / `setPathName` / `setPathVisible` / `setActivePath`
- 加**分级信号** `pathsChanged()`（对照 `pixelsChanged(QRect)` 的分级思路）
  — **不要**每帧发；只在结构或内容变化时发一次

### 6.2 `ui/pathtreepanel.cpp` 接线

- `refreshFromDocument()`：真读 `doc->paths()`，列出 `path.name`，选中项高亮
- `onNewItem()`：`doc->addPath(Path{})` + 压撤销
- `onDeleteItem()`：`doc->removePath(idx)` + 压撤销
- 底栏四个图标按钮接上：
  | 图标 | 行为 |
  |---|---|
  | `new-path.svg` | 新建空路径 |
  | `from-selection.svg` | 选区 → 路径（见 6.4） |
  | `to-selection.svg` | 路径 → 选区（调 `PathFillOp` + `combineShapeMask`） |
  | `stroke.svg` | 描边路径（调 `PathStrokeOp`） |

### 6.3 新增 `PathUndo`（`app/undoitem.h/.cpp`）

照 `SelectionUndo` 写：

```cpp
/**
 * 路径撤销（整条路径快照）。
 *
 * 【为什么不增量】GIMP 的 gimppathundo.c 也只有 228 行 —— 它同样用粗粒度快照。
 * 一条路径几百锚点 ≈ 几 KB，UndoItem::byteSize() 完全扛得住；而"记哪个锚点动了多少"
 * 的增量方案会引入"撤销后索引失效"这类难查 bug，不值得。
 */
class PathUndo final : public UndoItem { /* before / after 两份 Path + 索引 */ };
```

### 6.4 选区 → 路径（`from-selection`）

**算法（必须按此实现，别自创）**

1. 取 `Selection::mask()`（灰 mask）
2. 二值化（`> 127` 视为内部）
3. **Moore 邻域轮廓跟踪**得外/内轮廓点序列
   - 用 `QImage::constScanLine` 逐行扫，**不要**用 `pixel()`（本项目已有这个性能教训）
   - 先扫到第一个内部像素作为起点，再沿边界跟踪；已访问标记用 `QBitArray`
4. 每个轮廓做**闭合折线简化**（Douglas–Peucker，`epsilon ≈ 1.0` 文档像素）
5. 简化后的点集**直接作为锚点**，调 `recomputeAutoHandles(tension)` 得到平滑曲线
6. 产出 `Path`（外轮廓 + 各内轮廓作为子路径），加进文档

> 为什么用 Catmull-Rom 而不是 PS 的精确拟合：PS 的"建立工作路径"有自己的误差参数与
> 拟合算法（未公开）。我们用 Douglas–Peucker + 插值样条，**观感足够且可验证**。
> 把「容差固定 1.0 px、不提供对话框」写进 `features.md` 限制节。

### 6.5 `.pslite` 持久化（`io/projectio.cpp`）

> ⚠️ **与现行 IO 政策对齐**：`ProjectIo` 只认 `ProjectFormat::Current`，**不做跨版本兼容**
> （见 `projectio.h`）。路径入库 = **升版本**，不是「旧档静默无路径」。

- **`ProjectFormat::Current` +1**（例如 3 → 4）
- 路径段写在图层段之后：`路径数 → 每条 {name, visible, 子路径数 → {closed, 锚点数 → 每个锚点 6 个 double + 3 个 bool}}`
- **旧版文件**：`format != Current` ⇒ **明确报错**（与现有「工程格式不匹配」一致）；**不**假装打开空路径
- **新增用例断言**（扩展 `t_projectio`）：
  1. 存 3 条路径（含闭合/多子路径/显式控制柄/尖角）→ 读回 → **每锚点浮点逐位相等**
  2. 构造/保留一份 **v3（无路径段）样本** ⇒ `load` 失败且错误信息含格式不匹配（政策护栏）

### 6.6 新用例 `t_pathtree`（面板接线）

- 新建/删除路径真的改文档、各留**一条**撤销
- 名称/眼睛改动真的驱动 domain
- 选区↔路径互转后 `Selection` 与 `paths()` 都变、各留一条撤销

---

## 7. P4 —— 三个锚点工具独立化

**不要复制三份逻辑。** 新增 `tools/patheditops.h/.cpp`（**纯函数，无 Qt widget 依赖**）：

```cpp
namespace Ps::PathEditOps {
    bool addAnchorAt(Path &p, int sub, int seg, qreal t);
    bool deleteAnchorAt(Path &p, int sub, int idx);
    bool toggleCorner(Path &p, int sub, int idx);
}
```

`PenTool` 的 Ctrl/Alt/线段点击分支**直接调这三个函数**；
`AddAnchorPoint` / `DeleteAnchorPoint` / `ConvertPoint` 三个新工具类各自只做
"命中 → 调对应函数 → 压撤销"，**没有自己的几何逻辑**。

三个工具各自**必须**在 `toolmanager.cpp` 注册，并在 `t_pentool` 里加"选中它们后
点击真的改了路径"的断言（替换第 13 条）。

---

## 8. 明确不做（写进文档，避免被"顺手做了"）

| 不做 | 理由 |
|---|---|
| **矢量蒙版**（路径当图层蒙版） | 用户已确认不碰；需要 `LayerMask` 支持矢量，属另一条线 |
| **SVG 导入/导出** | GIMP 那边 2282 行，其中 arc→cubic 换算就够写一天 |
| **路径布尔运算**（并/交/差） | PS 有、GIMP 没有；不在本次范围 |
| **路径变形 / 沿路径弯曲**（`gimppath-warp.c`） | 不做 |
| **文字转路径 / 形状转路径** | 不做 |
| **PSD 矢量路径读写** | `.psd` 导出仍只带图层像素 |
| **路径栅格化上 GPU** | 逐扫描线求交是数据相关分支，不适合当前 compute-shader 后端（`gpu-plan §4.4.1`） |

---

## 9. 每阶段的完成定义（缺一不算完）

1. **代码**：按本文件写；**关键算法有中文注释，且写"为什么"**
2. **用例**：本阶段列出的断言全部实现且**通过**；`36+N/36+N` 全绿
3. **文档同步**：
   - `docs/code-map.md`：新增文件各加一行（职责 + 为何重要）
   - `docs/features.md`：写"当前限制"（见 §10）
   - `docs/tests.md`：登记新用例
   - `docs/pending-dev.md`：若踩坑或有缺陷，登台账（现象/根因/改法/防复发）
4. **构建**：主程序 `PSLite.exe` 干净重建 0 error
5. 提交文案按 `.cursor/rules/docs-and-commit.mdc` 的格式给（**不擅自 commit**）

---

## 10. 必须写进 `features.md` 限制节的 5 条

1. **填充规则已是 non-zero（WindingFill）**，对齐 PS；自交路径按环绕数判定。
2. **弯度钢笔用插值 C¹ 样条（Catmull-Rom → 贝塞尔）**，观感对齐 PS，
   **不是**逐点复刻 Adobe 私有算法；`tension` 是可按观感调的常数。
3. **选区→路径**用 Moore 轮廓跟踪 + Douglas–Peucker（`epsilon = 1.0 px`），
   **不提供** PS 的"误差"对话框。
4. **描边**复用画笔 dab 体系，因此与 PS 的纯色描边观感略有不同（间距系数可调）。
5. **路径不参与 GPU 加速**；大图（> 8 Mpx）填充为毫秒级，锚点数 > 2000 时给提示。

---

## 11. 落地顺序（每步独立可验收）

| 步 | 内容 | 交付物 | 门槛 |
|---|---|---|---|
| **1** | P0 数据模型 + 贝塞尔 | `domain/path.*`、`engine/bezier.*`、`t_pathgeom` | 13 条断言全绿；`36+1/36+1` |
| **2** | P1 栅格化 + 选区接线 | `pathfillop`、`pathstrokeop`、`t_pathraster` | 8 条全绿；含**第 3/4/5 条 non-zero 护栏** |
| **3** | P2 钢笔基础交互 | `tools/pentool.*`、注册 3 个 ID、`t_pentool` 1–10 条 | 全绿 |
| **4** | P2 收尾：修饰键 + 弯度/自由钢笔 | 同上扩展、`t_pentool` 11–13 条 | 弯度共线 + 不被覆盖两条必过 |
| **5** | P3 路径树 + 撤销 + 持久化 | `imagedocument` 路径 API、`PathUndo`、`projectio`、`t_pathtree` | Current 升版；旧版明确拒读 |
| **6** | P4 三个锚点工具 | `patheditops.*` + 3 个工具类 | 无重复几何逻辑 |

**建议每次只做一步并提交**，便于回滚。

---

## 12. GIMP 关键落点速查（已核对）

| 主题 | GIMP 位置 |
|---|---|
| 路径容器：多 stroke + 闭合标志 | `app/path/gimpstroke.h:36-44`（`gint id; GQueue *anchors; gboolean closed;`） |
| 锚点类型（锚点/控制柄分离） | `app/path/path-enums.h:24-28`（`GIMP_ANCHOR_ANCHOR` / `_CONTROL`） |
| 控制柄联动策略 | `path-enums.h:30-36`：`FEATURE_NONE / EDGE / ALIGNED / SYMMETRIC` ← **PS 的 Alt/裸拖 = NONE vs ALIGNED** |
| 贝塞尔 stroke 全部操作 | `app/path/gimpbezierstroke.c`（2621 行） |
| 插入/删除/转换锚点 | `gimpbezierstroke.h:77` + `gimpstroke.h:264-278` |
| 求值/扁平化 | `gimpstroke.h:301-320`（`get_length` / `get_point_at_dist` / `interpolate` / `make_bezier`） |
| 扁平化结果类型 | `app/core/gimpbezierdesc.c/.h`（191/43 行） |
| 路径→选区 | `app/path/gimppath.c:155,260,875`（`gimp_path_to_selection`，是 `GimpItem::to_selection` 实现） |
| 交互命中与绘制 | `app/display/gimptoolpath.c`（2121 行） |
| 工具状态机 | `app/tools/gimppathtool.c`（1196 行） |
| 撤销粒度 | `app/path/gimppathundo.c`（仅 228 行 ⇒ 粗粒度快照） |
