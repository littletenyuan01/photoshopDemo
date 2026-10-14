# 瓦片页缓存（Tile Page Cache）开发计划

> 状态：**A+B 已落地**（默认预算 0 = 换页关闭；`PSLITE_TILE_CACHE_MB` opt-in）　·　2026-10
> 对照实现：GIMP `app/core/gimptile.c` / `gimptile-cache.c`（2.10）、GEGL `gegl-buffer` 瓦片语义
> 相关：[architecture.md](architecture.md)（投影与脏矩形）、[tech-notes.md](tech-notes.md)、[pending-dev.md](pending-dev.md)
> 代码：`psDemo/engine/cache/{tilecache,swapstore,memstats}.*`；用例 `tilecache`

---

## 0. 一句话结论

**本工程没有页缓存 —— 而且是当年"明确去掉"的**（`psDemo/domain/tilebuffer.h:30` 原话：

> 【对照】GIMP GeglBuffer 瓦片语义；**本 Demo 去掉 COW / 交换 / 全局空瓦片池**）。

**值得做，但要"小做"**：只补 **"交换"** 这一件（+ 配套的内存预算与可观测），
**不做** GIMP 那套完整的 per-tile 引用计数。
推荐分两档落地：**A 档（可观测 + 预算 + 淘汰派生缓存，1–2 天）** →
**B 档（瓦片磁盘换页，4–7 天）**。合计 **一人 1.5–2 周**。

**明确不推荐**：C 档（undo 瓦片化 + 完整 COW），5–10 天且收益不抵风险（§7）。

---

## 1. 现状盘点（有证据，不靠印象）

| 能力 | 本工程状态 | 证据 |
|---|---|---|
| **瓦片化存储** | ✅ **已有**：64×64，稀疏 `QHash<qint64, QImage>`，只在被写到的格子分配 | `tilebuffer.h:37 kTileSize=64`、`:119 QHash<qint64,QImage> m_tiles`、`:22`「不整层 malloc」 |
| **脏矩形 + 分块缓存 + 视口优先 + 空闲渐进** | ✅ **已有**（当年就是照 GIMP 做的） | `engine/projection.h`：`setPriorityRect`、`kIdleChunkBudget=16`、`processPendingChunks`、`invalidateChunks` |
| **撤销内存上限** | ✅ 有，但**靠丢最老历史**实现（丢了就撤不回去） | `app/historystack.h:32`「默认约 256MB 上限，**超限丢最老 undo**」；`:79` `while (m_undoBytes > m_byteLimit && m_undo.size() > 1)` 丢 `front()` |
| **瓦片淘汰 / 内存预算** | ❌ **没有**。写过的瓦片**永不释放**，随触碰的格子数单调增长 | domain 全局搜不到 `LRU/evict/上限/budget/swap`（只有 `HistoryStack` 自己的字节计数） |
| **磁盘换页（页缓存本体）** | ❌ **没有** | `tilebuffer.h:30` 明写"去掉 交换"；`:27`「无全局共享 zero-tile / **scratch**」 |
| **COW / 全局空瓦片池** | ❌ 容器层没有 | 同上。**但见 §6.1 的重要澄清** |
| **区域滤镜结果缓存** | ✅ **有界**（每层**单槽**） | `domain/layer.h:336 m_regionFilterRect` + `:337 m_regionFilterImage` ⇒ 不泄漏 |
| **投影缓冲** | ⚠️ **整画布稠密 `QImage`**（不是稀疏的） | `engine/projection.h:77 const QImage &image() const { return m_buffer; }` |

**⇒ 缺口只有一个：瓦片常驻内存无上限、无淘汰、无换页。**
其余（投影缓存、滤镜区域缓存、undo 上限）都已经有界或有明确策略。

### 1.1 这个缺口现在会不会痛？

- **1600×1200 全画满一层** = 7.3 MB；20 层 ≈ **147 MB**；投影再 7.3 MB ⇒ 现在**跑得动**。
- **2000×1500**（tech-notes 里出现过的尺寸）一层 11.4 MB，20 层 ≈ 229 MB。
- 加上 undo 上限 256 MB ⇒ **峰值 ~500 MB**。桌面能活，但已经不小。
- **真正会痛的场景**：6000×4000（96 MB/层）× 20 层 = **1.9 GB** ⇒ 会 OOM / 疯狂换页。
  而"能开大图"正是页缓存存在的唯一理由。

**⚠️ 所以第一步不是写换页，而是先量出来**（§3 A 档）：本工程现在**连"当前用了多少瓦片内存"都打不出来**，
没有这个数就无法定预算、也无法证明换页有效。这也符合本工程的既有纪律「**先测再改**」。

---

## 2. GIMP 的机制（只留可移植的"主逻辑"）

GIMP 2.10 的页缓存集中在 `gimptile.c`，核心小得惊人：

```
GimpTile {
    GimpTileCache *cache;      // 全局单例
    gboolean dirty;            // 内存副本是否新于 swap
    gboolean shadow;           // ★ 影子瓦片：与他人共享数据，写时再拷
    guchar   *data;            // NULL = 当前不在内存（或在 swap 里 / 未分配）
    gulong    swap_offset;     // 在 swap 文件里的偏移
    gint      ref_count;       // ★ 引用计数
}

gimp_tile_ref(tile)    : data==NULL ⇒ gimp_tile_cache_load(tile)（从 swap 读回）; ref_count++
gimp_tile_unref(tile)  : ref_count--; 归零 ⇒ gimp_tile_cache_flush(tile)
gimp_tile_cache_flush(): dirty ⇒ 写回 swap；free(data)；cache->size -= tile_bytes
gimp_tile_cache_trim() : while (cache->size > cache->max_size) 淘汰 **LRU** 瓦片
```

**可移植的四条主逻辑**：

| # | GIMP 的做法 | 为什么它是"主逻辑" |
|---|---|---|
| 1 | **三态瓦片**：内存(干净/脏) / 在 swap / 未分配 | "未分配"这一态本工程已有（稀疏 hash 的空缺）；差的是"在 swap" |
| 2 | **单一 swap 文件 + 每瓦片一个 `swap_offset`** | 实现极小：一个 `QTemporaryFile` 追加写，偏移记在瓦片上 |
| 3 | **全局字节预算 + LRU 淘汰** | 上限是**可配置的一个数**，不是启发式 |
| 4 | **脏才写回**（clean 直接丢，需要时从 swap 读） | 省一半 I/O；对"只读回放"场景尤其重要 |

**GIMP 3.x 已经把瓦片下沉到 GEGL**，GIMP 侧只剩 91 行适配壳
（`gimptilehandlerprojectable.c`）—— 说明这套东西**本身就该是一层薄封装**，不该长成框架。
本计划照这个尺度做。

---

## 3. ⚠️ 关键工程约束：本工程**不能**照搬 `ref_count`

这是本计划最重要的一节，决定了整个工程量。

GIMP 敢用 per-tile `ref_count`，是因为它的**每次瓦片访问都必须走 `gimp_tile_ref/unref`**（成对、有纪律）。
本工程不是这样：

```cpp
// psDemo/domain/tilebuffer.h
QImage *ensureTile(int tx, int ty);        // ★ 裸指针交出去
QImage *tileAt(int tx, int ty);            // ★ 裸指针交出去
const QImage *tileAt(int tx, int ty) const;
```

调用方（画笔、合成器、滤镜、投影）会**长期持有这个 `QImage*`**。
若缓存在这期间淘汰该瓦片 ⇒ **悬垂指针**（而在 Windows 上表现为随机 `0xC0000005`，正是台账 #21 那一类最难查的崩）。

**两条路**：

| 路 | 做法 | 代价 |
|---|---|---|
| **★ 采用：粗粒度"钉住"（pin per buffer）** | **不引入 per-tile 引用计数**；改为"某个 `TileBuffer` 正在被使用时，整个 buffer 钉住"，淘汰**只发生在静默点**（且跳过被钉住的 buffer） | 改动集中；API 不变；代价是**淘汰粒度粗**（一次操作期间该层瓦片不能被换出）—— 对本工程完全够用 |
| 不采用：完整 `gimp_tile_ref/unref` | 引入 RAII `TileRef`，逐处改造所有访问点 | 会碰到**几乎所有像素路径**；风险与工程量都上一个量级 |

> **判据**：本工程一次操作（一笔画、一次滤镜、一次投影同步）通常只涉及**少量图层**。
> 钉住这几层、允许其它层的瓦片被换出，已经能拿到页缓存 90% 的收益，而不用动 API。

---

## 4. 方案（推荐 A + B，C 不做）

### A 档 —— 先可观测、再定预算、再淘汰"可重算的缓存"（**1–2 天**）

1. **内存口径（必做第一步）**：`PSLITE_MEM=1` 时打印一行
   `[mem] tiles=…MB (n=…) undo=…MB projection=…MB swap=…MB peak=…MB`
   （`TileBuffer::allocatedTileCount()` 已经有了，补一个 `bytes()` 即可）
2. **瓦片预算（只记账、先不淘汰）**：`PSLITE_TILE_CACHE_MB`（默认 0 = 不限），超限时**打日志**。
   —— 这样能先拿到"真实工作负载下会不会超"的数据，再决定默认值
3. **淘汰"可重算"的派生缓存**（零正确性风险，因为这些数据丢了能重算）：
   - `Layer::m_regionFilterImage`（单槽，换代即失效）
   - `Projection` 的 chunk 有效位（已有 `invalidateChunks`，加一个"整片失效"入口即可）
   - **注意**：A 档**不动** `HistoryStack` 的丢历史策略（那是产品决策，见 §7）

> **A 档做完就已经解决"内存只涨不降"的一半**，而且风险接近零。

### B 档 —— 瓦片磁盘换页（GIMP 主逻辑，**4–7 天**）

```
新增 engine/cache/tilecache.{h,cpp}          // 全局单例（或每文档一个）
新增 engine/cache/swapstore.h                // 抽象：qint64 put(bytes) / get(offset,n) / 可注入假实现
TileBuffer 的 m_tiles 由 QHash<qint64,QImage>
            改为 QHash<qint64,TileSlot>
            TileSlot { QImage img; qint64 swapOffset=-1; bool dirty=false; qint64 lastUse=0; }
```

**接口（照 GIMP，但改成粗粒度钉住）**：

```cpp
class TileCache {
public:
    static TileCache &instance();
    void   setBudgetBytes(qint64 bytes);   // 0 = 不淘汰
    qint64 usedBytes() const;
    // 瓦片级
    QImage *load(TileBuffer *owner, qint64 key);             // swap→内存（或新建空白）
    void    markDirty(TileBuffer *owner, qint64 key);
    // 钉住：一次操作期间跳过被钉住的 buffer（替代 per-tile ref_count）
    void    pin(const TileBuffer *b);   void unpin(const TileBuffer *b);
    void    trim();                     // 只在静默点调用
    struct Stats { qint64 memBytes, swapBytes; int evicted, loaded, swapFailures; };
    Stats stats() const;
};
```

**四个决定性细节**：

| 细节 | 决定 |
|---|---|
| **换页文件** | 一个 `QTemporaryFile`（默认 `QDir::tempPath()`，可用 `PSLITE_TILE_CACHE_DIR` 指定），**只追加**；`swap_offset` 单调增长；退出即删。**不做**空间回收（碎片可接受，GIMP 也不做） |
| **瓦片载荷** | 固定 `kTileSize*kTileSize*4 = 16 KB`（边缘瓦片也按满格存，简单；浪费 <1%）。**先不压缩**；压缩（`qCompress`）列为可选后续（§7） |
| **淘汰时机** | 只在**静默点**：`Projection::sync` / `processPendingChunks` 之后、画笔抬笔、`markDirty` 之后、undo push 之后。**不在** `ensureTile` 里淘汰（那会引发重入与悬垂） |
| **淘汰对象** | `usedBytes > budget` 时按 `lastUse` 淘汰**最久未用且未被钉住**的瓦片；**脏的先写回 swap**，干净的直接丢（数据已在 swap） |
| **失败降级** | swap 打不开 / 写失败 / 读失败 ⇒ **该瓦片留在内存、只记一次日志、关闭换页**（与 gpu-plan「硬约束 2」同一纪律：**不许因为可选优化而失败**） |
| **开关** | `PSLITE_TILE_CACHE_MB`（0=关闭换页，默认）、`PSLITE_TILE_CACHE_LOG=1`、`PSLITE_TILE_CACHE_DIR` |

**放在哪一层**：`engine/cache/`（不依赖 Widgets，符合分层）。
`swapstore.h` 做成接口是为了**测试能注入内存假实现**——与 GPU 的 `setGpuBackendForTest` 同一手法。

---

## 5. 验收（照工程既有纪律写，全都能自动化）

| # | 验收项 | 怎么验 |
|---|---|---|
| 1 | **换页往返 bit-exact** | 写瓦片 → 强制淘汰 → 读回 ⇒ `diffCount == 0`（新用例 `tilecache`） |
| 2 | **预算被遵守** | `PSLITE_TILE_CACHE_MB=8` 下打开并编辑大文档，`usedBytes()` 峰值 ≤ 限额（+ 一个瓦片的余量） |
| 3 | **LRU 生效** | 连续淘汰时，最久未用的先出（用假 `SwapStore` 记录顺序断言） |
| 4 | **不悬垂** | 压力用例：大文档 + 画笔 + 投影 + 撤销反复；**全量连跑 ≥20 次 0 崩溃** |
| 5 | **既有用例不放宽** | 全部常驻用例（现 30+）默认（换页关闭）**必须全绿**；打开换页再跑一遍也要绿 |
| 6 | **失败降级** | 注入"总是失败的 `SwapStore`" ⇒ 结果仍正确、只记一次日志、自动关闭换页 |
| 7 | **性能护栏** | 换页开启但**未触发**淘汰时，端到端退化 ≤ **5%**（避免为内存把速度赔光） |
| 8 | **可观测** | `PSLITE_MEM=1` 能打出 tiles/undo/projection/swap/peak 五项 |

---

## 6. 不做什么（非目标，避免长成框架）

- ❌ **per-tile `ref_count` / RAII `TileRef`**（§3 已说明理由）
- ❌ **COW 影子瓦片**（§6.1 说明为何收益低）
- ❌ **swap 空间回收 / 压缩 / mmap**（GIMP 也不做；压缩列为可选后续）
- ❌ **undo 瓦片化**（C 档，§7）
- ❌ **投影缓冲稀疏化**（另一个量级，见 §7 末尾）
- ❌ **多进程 / 共享内存瓦片服务**（PS 的 scratch disk 那套，远超本工程）

### 6.1 重要澄清：**COW 其实已经有一半了**

`tilebuffer.h:30` 写"去掉 COW"，但 **Qt 的 `QImage` 本身就是隐式共享（copy-on-write）**：

- `TileBuffer` 被拷贝时，`QHash` 复制（廉价），其中每个 `QImage` **共享同一份像素**；
- 只有当某人拿**非 const** 指针写某块瓦片时，`QImage::detach()` 才为**那一块**深拷贝。

⇒ **"复制图层 / 历史快照"的像素代价在很大程度上已经自动省掉了**（前提是写方按瓦片取非 const 指针，而不是整层取）。
所以 **GEGL 的 shadow-tile COW 对本工程是重复建设**，不值得做。

> ⚠️ **但这一条要实测确认**（A 档顺带做）：复制一个 1600×1200 已画满的图层，
> 断言**内存增量远小于 7.3 MB**。若实测发现增量 ≈ 整层，说明有代码走了整层非 const 访问
> （把隐式共享打断了）—— 那才是真正该修的，而且**修它比做页缓存便宜得多**。**这是本计划的第一个检查点。**

---

## 7. 为什么 C 档（undo 瓦片化）不做

| | 现状 | 若做 C 档 | 判断 |
|---|---|---|---|
| undo 内存 | 有 256 MB 上限，**超限丢最老** | 改为存瓦片引用 + 换页 ⇒ "不丢历史也不涨内存" | 收益真实，但**改动大**：`LayerPixelsUndo` 现在存的是 `QImage`（见 `undoitem` 的 `byteSize()`），要改成瓦片级增量快照，牵动撤销/重做/`DocumentGeomUndo` 的 `LayerState` |
| 工程量 | — | **5–10 天** | 风险集中在本工程最需要稳定的地方（撤销正确性，台账里已有多条 undo 相关坑） |
| **替代方案** | 把 `m_byteLimit` 做成**用户可调**（如 128 MB / 256 MB / 1 GB），并在超限丢历史时**明确提示** | **半天**，解决"用户觉得历史莫名消失"的体感问题 | ✅ **推荐这个替代** |

> **结论**：C 档的诉求（"历史别丢、内存别涨"）用一个可调上限 + 明确提示能解决八成，
> 而代价是 1/20。等真有"必须开 1 亿像素文档"的需求再谈 C 档。

**另一笔记在案但不在本计划内**：`Projection::m_buffer` 是**整画布稠密图**
（6000×4000 = 96 MB，且是要显示的东西，不能淘汰）。若要支持超大画布，
"投影只常驻视口 + 邻域"是更根本的一步 —— 但它动的是渲染主链路，**风险远大于页缓存**，
应单独立项、单独评估。

---

## 8. 任务清单

### A 档（先做，1–2 天）

- [x] **P1** `TileBuffer::residentBytes()`（边缘按实际 `sizeInBytes`）+ `MemSnapshot` 峰值
- [x] **P2** `PSLITE_MEM=1` 打印 `tiles / undo / projection / swap / peak` 五项（`memstats.*`）
- [x] **P3** `PSLITE_TILE_CACHE_MB`：记账 + 超限打日志；预算 >0 时静默点才淘汰
- [x] **P4** ★ **检查点**：原 `duplicateLayer` 走 `materialize` 会打断 COW → 已改为 `sharePixelsFrom`；`tilecache` 断言同瓦片 `cacheKey` 共享
- [x] **P5** 淘汰可重算的派生缓存（`Layer::clearRegionFilterResult`、`Projection::invalidateChunkValidity`）
- [x] **P6** 用例 `tilecache`：记账 / 派生缓存淘汰后投影仍可重算

### B 档（A 档数据支持后做，4–7 天）

- [x] **P7** `engine/cache/swapstore.h`（`FileSwapStore` + `MemorySwapStore` + `FailingSwapStore`）
- [x] **P8** `engine/cache/tilecache.{h,cpp}`：预算 / LRU / 三态 / `trim()` / `Stats` / 粗粒度 `pin`
- [x] **P9** `TileBuffer` 改 `TileSlot`；`ensureTile`/`tileAt` 按需从 swap 读回；`TilePinGuard`
- [x] **P10** 静默点接入 `trim()`（`Projection::sync` / `processPendingChunks` / 抬笔 / `HistoryStack::push`）
- [x] **P11** 失败降级：swap 不可用 ⇒ 留内存 + 记一次日志 + 关闭换页
- [x] **P12** 用例 `tilecache`：往返 bit-exact / 预算遵守 / LRU / 失败降级 / COW 复制
- [x] **P13** 性能护栏实测：见下方 §8.1；用例断言粗护栏 ≤25%，目标 ≤5%
- [x] **P14** 文档：`architecture.md` / `tech-notes.md` / `pending-dev.md` / `tests.md` / `README.md` / `tiles-and-memory.md`

### 8.1 P13 实测（2026-10，本机 MinGW Release）

| 项 | 值 |
|---|---|
| 负载 | 1600×1200 实色层，`materialize` ×**40**（预热 1 轮不计） |
| 对照 | `budget=0`（关） vs `budget=512MB` + `MemorySwapStore`（开但**不淘汰**） |
| 5 连跑退化 | +0.8% / −1.6% / +2.2% / +6.7% / +2.4%（均值 ≈ **+2.1%**） |
| 时程 | off/on 均约 120–140 ms |
| 结论 | **满足目标 ≤5%**（均值）；单次偶发 >5% 属计时噪声，不作产品回归依据 |
| 用例 | `tilecache` 打印该行；断言粗护栏 ≤25%（防热路径灾变） |

> 若日后把 `touch`/字节记账做成每像素热路径，此断言会先红。

---

## 9. 风险

| 风险 | 缓解 |
|---|---|
| **悬垂 `QImage*`**（最危险，症状是随机崩溃） | 只做**粗粒度钉住 + 静默点淘汰**；压力用例 + 全量连跑 ≥20 次 |
| 淘汰发生在错误时机（重入） | **绝不在 `ensureTile` 里淘汰**；只在 4 个静默点 |
| swap I/O 拖慢交互 | 预算默认先 **0（关闭）**；只在实测超限的机器/文档上开；性能护栏用例兜底 |
| 换页掩盖了真正的内存 bug（如隐式共享断裂） | **P4 检查点前置**：先确认没有"该省没省"的地方 |
| 工程量蔓延成框架 | 非目标清单（§6）+ GIMP 3.x "91 行适配壳"的尺度参照 |

---

## 10. 工期与拍板项

| 档 | 内容 | 粗估（一人） |
|---|---|---|
| **A** | 可观测 + 预算记账 + 派生缓存淘汰 + **P4 检查点** | **1–2 天** |
| **B** | 瓦片磁盘换页（GIMP 主逻辑，粗粒度钉住） | **4–7 天** |
| **C** | undo 瓦片化 / 完整 COW | **不建议**（用"可调上限 + 提示"替代，半天） |
| **合计（推荐 A+B）** | | **1.5–2 周** |

**需要拍板的三件事**：

| # | 问题 | 建议 |
|---|---|---|
| **Q1** | 做不做 B 档（真磁盘换页）？ | **已做**（默认关；大图时设 `PSLITE_TILE_CACHE_MB`） |
| **Q2** | 默认预算定多少？ | **默认 0（不换页）**，已按此落地 |
| **Q3** | undo 上限要不要做成可调 + 提示？ | **已做** `HistoryStack::setByteLimit` + `historyTrimmed` 信号；UI 提示可后续接面板 |
