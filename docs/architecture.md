# 模块结构（实现对照）

> 状态：骨架阶段。下方为目标结构；落地后把「计划」改为「已实现」并补文件路径。

## 目标分层

```text
UI (Qt)
  MainWindow / CanvasView / LayerPanel / ToolOptions
        ↓ 命令 / 信号
Tools
  BrushTool / EraserTool  （事件 → 绘制或 History 命令）
        ↓
Document
  ImageDocument / Layer / LayerStack / compositor
        ↓
Paint
  像素写入当前层（如 QImage）
        ↓
History
  Undo/Redo 栈
        ↓
IO
  打开图像 / 导出合成图
```

## 与 GIMP 参考对应（学习用）

| 概念 | GIMP（参考） | 本项目（计划） |
|------|--------------|----------------|
| 图层 | `app/core/gimplayer.*` | `Layer` + `QImage` 缓冲 |
| 绘制核心 | `app/paint/` | `Paint` / stroke 写入 |
| 工具 UI | `app/tools/` | `*Tool` + Qt 事件 |
| 合成 | GEGL 节点图 | v1：CPU 逐层合成 |
| 脚本/PDB | `app/pdb/` | **不做** |

## 当前代码结构

```text
psDemo/
  main.cpp           # QApplication 入口
  mainwindow.*       # 主窗口（空壳）
  psDemo.pro         # qmake 工程
```

## 数据流（目标）

1. 用户在 Tool 上操作 → 生成对 Document 的修改（或 History 命令）  
2. Document 更新 Layer 像素或属性 → 通知 UI  
3. Canvas 向 Document 取合成结果并绘制  
4. 导出时对合成结果编码为 PNG/JPEG  

实现变化时同步改本节与 `code-map.md`。
