# docs — 技术文档索引

本目录记录 **功能介绍、结构、重点代码、技术点**。  
代码有更新时必须同步更新此处（见 `.cursor/rules/docs-and-commit.mdc`）。

与 `wiki/` 的分工：

| 目录 | 用途 |
|------|------|
| `wiki/` | 目标、路线、构建入口、简历话术 |
| `docs/` | 实现级文档（随代码演进） |

## 学习路径（按用户工作流）

| 入口 | 内容 |
|------|------|
| [学习路径/README.md](学习路径/README.md) | **按操作流程带路**（非按功能开发先后）：新建→看见→绘制→撤销→…→导出；内存/页缓存嵌在 L2～L8；L11 GPU 进阶 + 测验 |
| [简历-项目经验.md](简历-项目经验.md) | 简历条目与口述骨架 |
| [面试题-PhotoshopLite.md](面试题-PhotoshopLite.md) | 按故事线问答要点 |

## 专题目录

| 目录 | 内容 |
|------|------|
| [ui/](ui/README.md) | 界面壳：审查收口、图标清单 |
| [layers/](layers/README.md) | 文档 / 画布 / 图层 / 合成 / 视图概念 |
| [engine/](engine/README.md) | 算法与算子：注册、调度、三段式生命周期、脏区上行 |
| [学习路径/](学习路径/README.md) | 工作流跟读；横切主题（瓦片内存、页缓存、GPU）在**第一次碰到的步骤**引入 |

## 文档列表

| 文档 | 内容 |
|------|------|
| [overview.md](overview.md) | 工程总览与当前能力 |
| [architecture.md](architecture.md) | 模块结构与数据流 |
| [学习路径/README.md](学习路径/README.md) | **按用户工作流跟代码**（新建→导出；测验-工作流.html） |
| [简历-项目经验.md](简历-项目经验.md) | 简历话术提纲（需能对应仓库） |
| [面试题-PhotoshopLite.md](面试题-PhotoshopLite.md) | 面试问答按故事线 |
| [graph-engine.md](graph-engine.md) | **图引擎（engine/graph）**：DAG / ROI 反向传播 / 节点缓存与脏传播 / 合成入图，对照 GEGL |
| [features.md](features.md) | 功能清单与使用说明 |
| [pending-dev.md](pending-dev.md) | **待开发**：已有功能的已知风险与完善方向 |
| [dev-plan.md](dev-plan.md) | **开发计划**：异步定稿 / 中值增量 cum / ROI 通用化 / 位移算子四条需求的可验证开发步骤、验收标准与踩坑 |
| [filter-roadmap.md](filter-roadmap.md) | **滤镜现状与开发计划**：68 项菜单项逐项状态（已接线 14 / 未做 54）、优先级判断标准、A–F 六档分批与「不做的项及其理由」 |
| [gpu-plan.md](gpu-plan.md) | **GPU 加速开发计划**：定位为**可选加速后端**（有 GPU 就加速、没有就 CPU，**不是替换**）；硬约束、链级决策规则与 5 ns/px 阈值、P0（只做壳与调度、不写 GPU）～P3 分期与验收 |
| [page-cache-plan.md](page-cache-plan.md) | **瓦片页缓存（Tile Page Cache）**：A+B **已落地**（默认关）；粗粒度钉住 + 静默点换页；`PSLITE_TILE_CACHE_MB` / `PSLITE_MEM`；用例 `tilecache` |
| [tests.md](tests.md) | **常驻测试**：30+ 无 GUI 断言用例、构建运行方式、写测试的约定与坑 |
| [code-map.md](code-map.md) | 重点代码与文件索引 |
| [tech-notes.md](tech-notes.md) | 技术点与设计决策 |
| [ui/ui-review.md](ui/ui-review.md) | UI 结构审查与收口 |
| [ui/iconfont-icons.md](ui/iconfont-icons.md) | iconfont 图标清单 |
| [layers/](layers/README.md) | 图层与文档概念（新建、PPI、合成、瓦片内存、**实现对照**） |
| [engine/operators.md](engine/operators.md) | **算子调用链与示例**：注册表 / 常驻实例 / prepare→process→finish / 脏区上行；缓冲与点两条路径的完整时序 |
| [engine/magnetic-lasso.md](engine/magnetic-lasso.md) | **磁性套索算法与标定**：局部吸附 vs live-wire、代价函数、参数标定表、GIMP 常量对照、贴边不稳的成因与修正 |
| [engine/roi.md](engine/roi.md) | **ROI（请求区 / 所需输入区）**：为什么滤镜绕不开它、三类算子判定表、对称外扩的局限、与 GEGL `get_required_for_output` 的对照、通用化迁移设计 |
| [scope-estimate.md](scope-estimate.md) | 图层/选区/蒙版等功能评估与工作量 |
| [completeness.md](completeness.md) | 除编辑能力外，怎样才算完善 |
| [cursor-role-prompt.md](cursor-role-prompt.md) | 可粘贴的 Cursor 角色 Prompt |

## 相关目录

| 目录 | 用途 |
|------|------|
| [`../presentations/`](../presentations/README.md) | **演示稿库**：每份一个子目录（离线交互页面），讲原理用，不参与编译 |
| `images/` | 文档截图（由验证程序按真实渲染路径生成） |

## 维护约定

1. 每完成一个小功能：更新相关文档 + 在对话中给出 commit 文案  
2. **每轮开发都要留痕（开发准则）**：只要动了代码，同一轮必须落盘文档，且每处改动要写清**为什么这么改**
   （起因 / 改前数据 / 改法与被否掉的方案 / 结果与守它的断言 / 仍欠）；**被数据推翻的假设也要记**。
   详见 `.cursor/rules/docs-and-commit.mdc` 的「每轮开发都要留痕」一节  
3. 新增模块时：补 `code-map.md`，必要时拆新文档并挂到本索引  
4. 写文档时标明「已实现 / 计划中」，避免简历口径夸大  
5. UI 类说明放 `docs/ui/`；文档/图层/合成概念放 `docs/layers/`；算法与算子调度放 `docs/engine/`  
