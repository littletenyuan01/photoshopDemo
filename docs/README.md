# docs — 技术文档索引

本目录记录 **功能介绍、结构、重点代码、技术点**。  
代码有更新时必须同步更新此处（见 `.cursor/rules/docs-and-commit.mdc`）。

与 `wiki/` 的分工：

| 目录 | 用途 |
|------|------|
| `wiki/` | 目标、路线、构建入口、简历话术 |
| `docs/` | 实现级文档（随代码演进） |

## 文档列表

| 文档 | 内容 |
|------|------|
| [overview.md](overview.md) | 工程总览与当前能力 |
| [architecture.md](architecture.md) | 模块结构与数据流 |
| [features.md](features.md) | 功能清单与使用说明 |
| [code-map.md](code-map.md) | 重点代码与文件索引 |
| [tech-notes.md](tech-notes.md) | 技术点与设计决策 |
| [cursor-role-prompt.md](cursor-role-prompt.md) | 可粘贴的 Cursor 角色 Prompt |

## 维护约定

1. 每完成一个小功能：更新相关文档 + 在对话中给出 commit 文案  
2. 新增模块时：补 `code-map.md`，必要时拆新文档并挂到本索引  
3. 写文档时标明「已实现 / 计划中」，避免简历口径夸大  
