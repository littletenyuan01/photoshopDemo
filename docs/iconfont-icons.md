# 工具图标（iconfont）

规则见 `.cursor/rules/icon-sources.mdc`：优先从 [iconfont.cn](https://www.iconfont.cn/) 下载；禁止 Adobe 官方图标。

## 当前采用

目录：`psDemo/resources/icons/tools/`（**英文文件名** PNG）  
资源前缀：`:/icons/tools/`（见 `resources.qrc`）  
接线：`ui/toolbox.cpp` → `buildToolSlots()`

### 已接入映射

| 工具 | 文件 |
|------|------|
| 移动 | `move.png` |
| 矩形选框 | `rect-select.png` |
| 椭圆选框 | `ellipse-select.png` |
| 套索 | `lasso.png` |
| 魔棒 | `magic-wand.png` |
| 裁剪 | `crop.png` |
| 吸管 | `eyedropper.png` |
| 画笔 | `brush.png` |
| 橡皮 | `eraser.png` |
| 油漆桶 | `bucket.png` |
| 渐变 | `gradient.png` |
| 文字 | `type-horizontal.png` |
| 矩形/椭圆/三角/直线 | `rectangle.png` / `ellipse.png` / `triangle.png` / `line.png` |
| 抓手 | `hand.png` |
| 缩放 | `zoom.png` |

同组工具（选框、填充、形状）共用一个工具栏占位，右键飞出菜单切换。

目录内尚有未接线图标（`pen.png`、`stamp.png`、`blur.png`、`type-vertical.png` 等），后续加工具时再挂。

## 自绘 SVG

`resources/icons/tool-*.svg` 仍保留作回退，工具箱已不再默认使用。
