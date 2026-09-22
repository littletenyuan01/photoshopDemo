# 重点代码索引

> 随代码增长持续补充：路径 + 职责 + 为何重要。

## 入口与窗口

| 文件 | 职责 | 状态 |
|------|------|------|
| `psDemo/main.cpp` | `QApplication` 入口 | 已实现 |
| `psDemo/mainwindow.h/.cpp` | 持有文档、菜单动作、挂接画布 | 已实现 |
| `psDemo/mainwindow.ui` | 主窗口布局；中央提升为 `CanvasView` | 已实现 |
| `psDemo/psDemo.pro` | 源文件与 `INCLUDEPATH` | 已实现 |

## domain（文档真相）

| 文件 | 职责 |
|------|------|
| `domain/blendmode.h` | 混合模式枚举（现仅 Normal） |
| `domain/layer.h/.cpp` | 单层像素与属性 |
| `domain/layerstack.h/.cpp` | 图层列表（`std::vector<unique_ptr>`） |
| `domain/imagedocument.h/.cpp` | 文档：尺寸、栈、活动层、信号 |

## engine

| 文件 | 职责 |
|------|------|
| `engine/compositor.h/.cpp` | 预乘 Alpha 的 Normal 合成；可按矩形脏区合成 |
| `engine/paintengine.h/.cpp` | 圆形 dab / 线段插值；画笔 SourceOver、橡皮 DestinationOut |

**要点**：`blendNormalPremultiplied` 按扫描线混合；绘制与合成分离（对齐 GIMP paint vs projection）。

## ui

| 文件 | 职责 |
|------|------|
| `ui/canvasview.h/.cpp` | 合成缓存显示；缩放/平移；画笔/橡皮/抓手/缩放工具事件 |
| `ui/layerpanel.ui/.h/.cpp` | 图层面板：列表/显隐/透明度/增删排序 |
| `ui/toolbox.ui/.h/.cpp` | 左侧工具箱 + 前/背景色（对齐 GIMP Toolbox 结构） |
| `ui/tooloptionsbar.ui/.h/.cpp` | 工具选项栏（名称 + 画笔直径） |
| `tools/toolid.h` | 工具枚举（对应 GIMP ToolInfo 思路） |

## 计划中

| 计划类型 | 预期职责 |
|----------|----------|
| `Selection` | 文档级选区 mask |
| `LayerMask` / `AdjustmentLayer` | 蒙版与调整层 |
| `HistoryStack` | 撤销 / 重做 |
| `RasterIO` / `ProjectIO` | 导出与工程文件 |

## 摘录约定

1. 写清文件路径与符号名  
2. 短说明「为什么重要」  
3. 非显然逻辑可附 5–20 行关键片段  

**代码注释**：关键类与算法须在源码中写中文注释，见 `.cursor/rules/code-comments.mdc`。
