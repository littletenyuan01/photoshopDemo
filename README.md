# photoshopDemo

基于 **Qt** 的轻量仿 Photoshop 图像编辑器（产品名 **PhotoshopLite**，产物 **PSLite.exe**），用于学习与简历展示。  
参考 GIMP 的架构思路，强调**关键编辑链路完整**，不追求功能 1:1 复刻。

## 文档

| 目录 | 用途 |
|------|------|
| **[docs/](docs/README.md)** | 功能介绍、结构、重点代码、技术点（**随代码更新**） |
| **[wiki/](wiki/Home.md)** | 目标、路线、构建、简历话术 |

工程规则（Cursor）：`.cursor/rules/`  
约定：每次改代码同步 `docs/`；每个小功能完成后给出 commit 文案（不擅自提交）。

## 工程

- 应用代码：`psDemo/`（`psDemo.pro`，`TARGET = PSLite`）
- 打开方式：Qt Creator 打开 `psDemo/psDemo.pro` 后构建运行

## GitHub 网页 Wiki（可选）

见 [wiki/GitHub-Wiki-Setup.md](wiki/GitHub-Wiki-Setup.md)。