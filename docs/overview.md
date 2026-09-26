# 工程总览

## 项目是什么

`photoshopDemo` 是用 **Qt 6 Widgets** 编写的轻量图像编辑 Demo，面向简历与面试演示。  
产品名 **PhotoshopLite**，运行产物 **PSLite.exe**（工程目录仍为 `psDemo/`）。  
参考 GIMP 的分层思路（文档 / 图层 / 绘制 / 工具 / 历史），用更小体量实现**可演示闭环**。

## 仓库布局

```text
photoshopDemo/
├── psDemo/                 # Qt 应用（qmake：psDemo.pro）
│   ├── app/                # 会话：AppSession（文档持有 + 广播）
│   ├── domain/             # 文档 / 图层（真相数据）
│   ├── engine/             # 合成等算法
│   ├── tools/              # 交互状态机：Tool 基类 + ToolManager + 各工具
│   ├── ui/                 # 视图与面板
│   ├── mainwindow.*        # 主窗口壳 + .ui
│   └── ...
├── docs/                   # 技术文档（本目录，随代码更新）
├── wiki/                   # 项目 Wiki（目标、路线、构建）
├── .cursor/rules/          # Cursor 工程规则
├── README.md
└── .gitignore
```

**分层依赖（强制单向）**：`ui → app → tools/domain`；`engine` 被 `tools`/`domain` 调用；
**`domain`/`engine` 不得依赖 Qt Widgets**。

## 当前已实现能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 主窗口 | 已实现 | 菜单：新建 / 打开 / 视图缩放；中央为画布 |
| 文档/图层模型 | 已实现（基础） | `ImageDocument` + `Layer` + `LayerStack`；分级信号 + 语义化 setter |
| 会话与广播 | 已实现 | `AppSession`：文档唯一持有者；面板订阅广播而非逐个手工同步 |
| 画布合成预览 | 已实现（基础） | `Compositor`（Normal+透明度）+ `CanvasView`（实现 `ViewPort`） |
| 工具层 | 已实现 | `ToolManager` + `Tool` 基类 + 移动/抓手/缩放/画笔橡皮；新增工具不改画布 |
| 图层面板 | 已实现（基础） | 新建/删除/显隐/透明度/上下移/重命名；增量更新 |
| 打开位图 | 已实现 | PNG/JPEG/BMP/WebP → 单层文档 |
| 缩放/平移 | 已实现 | 滚轮缩放；中键或 Alt+左键拖拽；适应窗口 |
| 画笔 / 橡皮 | 已实现 | 圆形 dab + 线段插值，写活动层 |
| 撤销 / 重做 | 计划中 | 收口点已就位（见 `docs/ui-review.md`） |
| 导出 | 计划中 | — |

## 技术栈

- 语言：C++17
- UI：Qt Widgets（界面用 `.ui`）
- 构建：qmake（`psDemo.pro`），Qt Creator 开发
- 像素：`QImage` Format_ARGB32_Premultiplied
- 算法 / 加速（允许）：**OpenCV**、**CUDA**、CPU 并行；无 GPU 时主链路应可回退 CPU

## 相关入口

- 构建说明：`wiki/Build.md`
- 架构：`docs/architecture.md`
- 功能：`docs/features.md`
- 代码索引：`docs/code-map.md`
