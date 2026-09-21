# 重点代码索引

> 随代码增长持续补充：路径 + 职责 + 为何重要。

## 入口与窗口

| 文件 | 职责 | 状态 |
|------|------|------|
| `psDemo/main.cpp` | `QApplication` 创建与事件循环 | 已实现 |
| `psDemo/mainwindow.h` | `MainWindow` 声明 | 已实现（空壳） |
| `psDemo/mainwindow.cpp` | 加载 `.ui` | 已实现（空壳） |
| `psDemo/mainwindow.ui` | Qt Designer 布局（界面改动优先改此文件） | 已实现（空中央区） |
| `psDemo/psDemo.pro` | qmake：C++17 + widgets；`FORMS` 列出全部 `.ui` | 已实现 |

**约定**：新增窗口/对话框/面板时增加对应 `.ui`，并加入 `FORMS`。画布等自绘控件可无代码实现，但其外层停靠与工具栏仍用 `.ui` 排布。详见 `.cursor/rules/qt-ui-forms.mdc`。

## 计划中的关键类型（占位名，落地后改名并补说明）
| 计划类型 | 预期职责 |
|----------|----------|
| `ImageDocument` | 文档：尺寸、图层列表、活动层 |
| `Layer` | 单层像素与属性（可见、透明度、混合） |
| `Compositor` | 多层合成到预览 `QImage` |
| `BrushTool` / `EraserTool` | 输入事件 → 像素修改 |
| `HistoryStack` | 撤销 / 重做 |
| `ImageIO` | 打开 / 导出 |

## 摘录约定

补充重点代码时：

1. 写清文件路径与符号名  
2. 用简短说明解释「为什么重要」，避免大段粘贴  
3. 若逻辑非显然，可附 5–20 行关键片段  
