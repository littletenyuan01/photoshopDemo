# 工程总览

## 项目是什么

`photoshopDemo` 是用 **Qt 6 Widgets** 编写的轻量图像编辑 Demo，面向简历与面试演示。  
参考 GIMP 的分层思路（文档 / 图层 / 绘制 / 工具 / 历史），用更小体量实现**可演示闭环**。

## 仓库布局

```text
photoshopDemo/
├── psDemo/                 # Qt 应用（qmake：psDemo.pro）
│   ├── main.cpp
│   ├── mainwindow.h/.cpp/.ui
│   └── ...
├── docs/                   # 技术文档（本目录，随代码更新）
├── wiki/                   # 项目 Wiki（目标、路线、构建）
├── .cursor/rules/          # Cursor 工程规则
├── README.md
└── .gitignore
```

## 当前已实现能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 主窗口 | 已实现 | 空 `QMainWindow`，待挂画布与面板 |
| 文档/图层模型 | 计划中 | — |
| 画布合成预览 | 计划中 | — |
| 画笔 / 橡皮 | 计划中 | — |
| 撤销 / 重做 | 计划中 | — |
| 打开 / 导出 | 计划中 | — |

## 技术栈

- 语言：C++17
- UI：Qt Widgets
- 构建：qmake（`psDemo.pro`），Qt Creator 开发
- 算法 / 加速（允许）：**OpenCV**、**CUDA**、CPU 多线程 / 并行；无 GPU 时主链路应可回退 CPU

## 相关入口

- 构建说明：`wiki/Build.md`
- 功能闭环定义：`wiki/Feature-Pipeline.md`
- 架构规划：`wiki/Architecture.md`（规划）与 `docs/architecture.md`（实现对照）
