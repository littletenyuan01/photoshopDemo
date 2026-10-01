# docs — 技术文档索引

本目录记录 **功能介绍、结构、重点代码、技术点**。  
代码有更新时必须同步更新此处（见 `.cursor/rules/docs-and-commit.mdc`）。

与 `wiki/` 的分工：

| 目录 | 用途 |
|------|------|
| `wiki/` | 目标、路线、构建入口、简历话术 |
| `docs/` | 实现级文档（随代码演进） |

## 专题目录

| 目录 | 内容 |
|------|------|
| [ui/](ui/README.md) | 界面壳：审查收口、图标清单 |
| [layers/](layers/README.md) | 文档 / 画布 / 图层 / 合成 / 视图概念 |
| [engine/](engine/README.md) | 算法与算子：注册、调度、三段式生命周期、脏区上行 |

## 文档列表

| 文档 | 内容 |
|------|------|
| [overview.md](overview.md) | 工程总览与当前能力 |
| [architecture.md](architecture.md) | 模块结构与数据流 |
| [features.md](features.md) | 功能清单与使用说明 |
| [pending-dev.md](pending-dev.md) | **待开发**：已有功能的已知风险与完善方向 |
| [code-map.md](code-map.md) | 重点代码与文件索引 |
| [tech-notes.md](tech-notes.md) | 技术点与设计决策 |
| [ui/ui-review.md](ui/ui-review.md) | UI 结构审查与收口 |
| [ui/iconfont-icons.md](ui/iconfont-icons.md) | iconfont 图标清单 |
| [layers/](layers/README.md) | 图层与文档概念（新建、PPI、合成、瓦片内存、**实现对照**） |
| [engine/operators.md](engine/operators.md) | **算子调用链与示例**：注册表 / 常驻实例 / prepare→process→finish / 脏区上行；缓冲与点两条路径的完整时序 |
| [engine/magnetic-lasso.md](engine/magnetic-lasso.md) | **磁性套索算法与标定**：局部吸附 vs live-wire、代价函数、参数标定表、GIMP 常量对照、贴边不稳的成因与修正 |
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
2. 新增模块时：补 `code-map.md`，必要时拆新文档并挂到本索引  
3. 写文档时标明「已实现 / 计划中」，避免简历口径夸大  
4. UI 类说明放 `docs/ui/`；文档/图层/合成概念放 `docs/layers/`；算法与算子调度放 `docs/engine/`  
