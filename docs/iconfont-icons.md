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

旧自绘 `tool-*.svg` 已删除；工具箱只使用 `:/icons/tools/*.png`。

## 图层面板底栏图标

目录：`psDemo/resources/icons/layers/`（英文文件名 PNG）  
资源前缀：`:/icons/layers/`  
接线：`ui/layertreepanel.cpp` 构造时 `setIcon`

| 按钮 | 文件 | 隐喻 |
|------|------|------|
| 链接图层 | `link.png` | 双环相扣 |
| 图层样式 | `fx.png` | fx 字样（行业习惯） |
| 图层蒙版 | `mask.png` | 方框 + 圆 |
| 调整图层 | `adjustment.png` | 半满圆 |
| 新建组 | `group.png` | 文件夹 |
| 新建图层 | `new-layer.png` | 叠层 + 加号 |
| 删除 | `delete.png` | 垃圾桶 |

说明：暗色主题线框自绘，**非** Adobe 官方资源；后续若从 [iconfont.cn](https://www.iconfont.cn/) 换更精致版本，可按同名替换 PNG。生成脚本：`layers/_gen_icons.py`。

## 通道面板底栏图标

目录：`psDemo/resources/icons/channels/`  
资源前缀：`:/icons/channels/`  
接线：`ui/channeltreepanel.cpp`

| 按钮 | 文件 | 隐喻 |
|------|------|------|
| 通道→选区 | `load-selection.png` | 虚线圈 + 箭头 |
| 选区→通道 | `save-selection.png` | 虚线圈 + 实心块 |
| 新建通道 | `new-channel.png` | 条带 + 加号 |
| 删除 | `layers/delete.png`（共用） | 垃圾桶 |

## 路径面板底栏图标

目录：`psDemo/resources/icons/paths/`  
资源前缀：`:/icons/paths/`  
接线：`ui/pathtreepanel.cpp`

| 按钮 | 文件 | 隐喻 |
|------|------|------|
| 填充路径 | `fill.png` | 实心多边形 |
| 描边路径 | `stroke.png` | 空心折线 + 锚点 |
| 路径→选区 | `to-selection.png` | 折线 + 虚线圈 |
| 选区→路径 | `from-selection.png` | 虚线圈 + 折线 |
| 新建路径 | `new-path.png` | 钢笔 + 加号 |
| 删除 | `layers/delete.png`（共用） | 垃圾桶 |

通道/路径图标生成脚本：`layers/_gen_channel_path_icons.py`。
