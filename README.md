# photoshopDemo

基于 **Qt 6 Widgets** 的轻量仿 Photoshop 图像编辑器（产品名 **PhotoshopLite**，产物 **PSLite.exe**）。  
参考 [GIMP](https://www.gimp.org/) 的分层思路，用可控代码量跑通**可演示的编辑闭环**——不追求功能 1:1 复刻。

## 演示闭环（v1）

```text
新建 / 打开 → 多层编辑 → 选区约束绘制 → 清除 / 填充
    → 撤销 / 重做 → 导出 PNG/JPEG（或存储 .pslite）
```

手测步骤见 **[wiki/Feature-Pipeline.md](wiki/Feature-Pipeline.md)**；约 3 分钟走完。

## 已实现（摘要）

| 模块 | 能力 |
|------|------|
| 文档 / 图层 | 瓦片缓冲、显隐 / 不透明度 / 混合（PS 27 种）、偏移移动 |
| 工具 | 移动、选框、画笔 / 橡皮、油漆桶 / 渐变、抓手 / 缩放 |
| 选区 | 矩形 / 椭圆 / 套索；全选 / 取消 / 反选；约束绘制与填充 |
| 历史 | 推入式撤销（像素 / 图层属性 / 结构 / 几何） |
| IO | 打开位图；`.pslite` 工程；导出 PNG/JPEG；PSD 子集写出 |
| UI | PS 风格菜单壳、主页最近文件、图层面板、颜色 / 属性面板 |

刻意未做：完整蒙版 / 调整层、PSD 完美兼容、GEGL / PDB。详见 [wiki/Project-Goals.md](wiki/Project-Goals.md)。

## 快速开始

1. 安装 Qt 6（Widgets + SVG）+ MinGW 或 MSVC  
2. Qt Creator 打开 `psDemo/psDemo.pro` → 构建运行  
3. 或命令行：见 [wiki/Build.md](wiki/Build.md)

## 仓库结构

```text
photoshopDemo/
├── psDemo/          # Qt 应用（qmake，TARGET = PSLite）
│   ├── app/         # 会话、最近文件、撤销栈
│   ├── domain/      # 文档 / 图层 / 选区（真相数据）
│   ├── engine/      # 合成、混合、绘制算法
│   ├── tools/       # 工具状态机
│   ├── io/          # 栅格 / 工程 / PSD
│   └── ui/          # 画布与面板
├── docs/            # 实现级技术文档（随代码更新）
├── wiki/            # 目标、路线、构建、简历话术
└── presentations/   # 离线原理演示稿（不参与编译）
```

依赖方向：`ui → app → tools/domain`；`domain` / `engine` **不依赖** Qt Widgets。

## 文档入口

| 目录 | 用途 |
|------|------|
| **[docs/](docs/README.md)** | 功能、架构、代码索引、技术决策 |
| **[wiki/](wiki/Home.md)** | 目标、路线、构建、[简历话术](wiki/Resume-Notes.md)、[演示说明](wiki/Demo.md) |

界面截图与面板预览：`docs/images/`（如 `right-panels.png`、`icons-preview.png`）。

## 约定

- 改代码同步更新 `docs/`  
- 小功能完成后给出 commit 文案（**不擅自提交**）  
- Cursor 规则：`.cursor/rules/`
