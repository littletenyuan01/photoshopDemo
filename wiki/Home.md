# photoshopDemo Wiki

仿 Photoshop 的轻量图像编辑器（Qt），用于学习与简历展示。  
参考开源项目 [GIMP](https://www.gimp.org/) 的架构思路，**不追求功能完整复刻**。

## 页面导航

| 页面 | 说明 |
|------|------|
| [项目目标](Project-Goals.md) | 定位、边界、代码量约束 |
| [功能链路](Feature-Pipeline.md) | 必须跑通的演示闭环 |
| [架构设计](Architecture.md) | 模块划分（参考 GIMP） |
| [构建与运行](Build.md) | 环境、编译、打开工程 |
| [开发路线](Roadmap.md) | 分阶段实现计划 |
| [简历话术](Resume-Notes.md) | 可写进简历/面试的要点 |
| [GitHub Wiki 启用](GitHub-Wiki-Setup.md) | 可选：网页 Wiki 标签 |

## 仓库结构（当前）

```
photoshopDemo/
├── psDemo/          # Qt 应用工程（qmake）
├── docs/            # 技术文档（功能/结构/重点代码，随代码更新）
├── wiki/            # 项目 Wiki（本目录：目标与路线）
├── .cursor/rules/   # Cursor 工程规则
├── README.md
└── .gitignore
```

实现级说明请看 → **[docs/README.md](../docs/README.md)**

## 相关链接

- 源码仓库：https://github.com/littletenyuan01/photoshopDemo
- 参考实现：仓库外的 `gimp-master`（GIMP 源码，仅作学习参考）
