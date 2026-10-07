# 工程总览

## 项目是什么

`photoshopDemo` 是用 **Qt 6 Widgets** 编写的轻量图像编辑 Demo，面向简历与面试演示。  
产品名 **PhotoshopLite**，运行产物 **PSLite.exe**（工程目录仍为 `psDemo/`）。  
参考 GIMP 的分层思路（文档 / 图层 / 绘制 / 工具 / 历史），用更小体量实现**可演示闭环**。

## 仓库布局

```text
photoshopDemo/
├── psDemo/                 # Qt 应用（qmake：psDemo.pro）
│   ├── app/                # AppSession、RecentDocuments、HistoryStack
│   ├── domain/             # 文档 / 图层 / 选区（真相数据）
│   ├── engine/             # 合成、混合、PaintEngine
│   ├── tools/              # Tool 基类 + ToolManager + 各工具
│   ├── io/                 # RasterIo / ProjectIo / PsdIo
│   ├── ui/                 # 画布与面板
│   ├── mainwindow.*        # 主窗口壳 + .ui
│   └── resources/          # 图标、样式、qrc
├── docs/                   # 技术文档（本目录，随代码更新）
│   ├── ui/                 # 界面壳：审查、图标清单
│   ├── layers/             # 文档 / 图层 / 合成概念
│   └── images/             # 文档截图
├── wiki/                   # 项目 Wiki（目标、路线、构建、演示）
├── presentations/          # 离线原理演示稿
├── .cursor/rules/          # Cursor 工程规则
├── README.md
└── .gitignore
```

**分层依赖（强制单向）**：`ui → app → tools/domain`；`engine` 被 `tools`/`domain` 调用；
**`domain`/`engine` 不得依赖 Qt Widgets**。

## 当前已实现能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 主窗口 / 主页 | 已实现 | PS 菜单壳；启动默认主页；最近文件 |
| 文档/图层模型 | 已实现 | `ImageDocument` + `TileBuffer`；分级信号 + 语义化 setter |
| 会话与广播 | 已实现 | `AppSession`：文档唯一持有者 |
| 画布合成预览 | 已实现 | PS 27 种混合 + 透明度；缩放/平移/像素网格 |
| 工具层 | 已实现 | 移动 / 选区 / 裁剪 / 吸管 / 图章 / 聚焦组 / 色调组 / **形状组** / 画笔橡皮 / 油漆桶渐变 / 抓手缩放 |
| 选区 | 已实现 | mask + 形状选区 + 魔棒洪泛 + 全选/取消/反选；约束绘制与填充 |
| 图层蒙版 | 已实现 | 灰度乘 alpha；涂画；应用/链接；可撤销；写入 `.pslite` |
| 清除 / 填充 | 已实现 | Delete / Shift+F5（前景色） |
| 图层面板 | 已实现 | 新建/删/复制/显隐/透明度/混合/重命名；调整层入口 |
| 打开位图 | 已实现 | PNG/JPEG/BMP/WebP → 单层文档 |
| 置入为图层 | 已实现 | 嵌入置入（居中）+ 链接置入（路径+缓存）；对照 GIMP 打开为图层 / 链接图层 |
| 调整图层 | 已实现 | 多类型 + 白蒙版 + 属性页；作用于下方合成 |
| 工程 / PSD | 已实现 | `.pslite`（`ProjectFormat::Current`）读写；PSD 子集写出 |
| 撤销 / 重做 | 已实现 | 像素/属性(含样式·滤镜)/结构/几何/选区；Ctrl+Z / Ctrl+Y |
| 导出 | 已实现 | PNG / JPEG 合成结果 |

## 技术栈

- 语言：C++17
- UI：Qt Widgets（界面用 `.ui`）
- 构建：qmake（`psDemo.pro`），Qt Creator 开发
- 像素：`QImage` Format_ARGB32_Premultiplied
- 算法 / 加速（允许）：**OpenCV**、**CUDA**、CPU 并行；无 GPU 时主链路应可回退 CPU（当前主链路为纯 Qt）

## 相关入口

- 构建说明：`wiki/Build.md`
- 演示说明：`wiki/Demo.md`
- 架构：`docs/architecture.md`
- 功能清单：`docs/features.md`
- 简历话术：`wiki/Resume-Notes.md`
