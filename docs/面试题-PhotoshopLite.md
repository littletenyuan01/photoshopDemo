# 面试题 · PhotoshopLite（按故事线）

配合 [学习路径](./学习路径/README.md)（按**新建→编辑→导出**工作流）与 [简历提纲](./简历-项目经验.md)。  
每题给「期望要点」；追问时落到类名/文件，勿背空话。

---

## A. 架构与会话

**Q1. 为什么要有 AppSession，而不是 MainWindow 持有文档？**  
要点：唯一所有权、`documentChanged` 广播、新面板零扇出；旧 doc 活到槽结束防悬垂。

**Q2. 依赖方向是什么？违反了会怎样？**  
要点：`ui→app→tools/domain`，engine 服务之；domain/engine 禁 Widgets → 难测、循环依赖。

**Q3. 启动时为什么先 opsInit？**  
要点：算子注册表；滤镜/合成/自检依赖类型已注册。

---

## B. 文档与像素

**Q4. 像素存在哪里？为什么用瓦片？**  
要点：`Layer::TileBuffer` 稀疏 64×64；大画布省内存、脏区对齐 chunk。

**Q5. 改不透明度为什么走 ImageDocument？**  
要点：语义化 API、通知、撤销、统一广播；避免 UI 拧 Layer 漏刷新。

---

## C. 合成与画布

**Q6. 画一笔从脏到屏幕经过谁？**  
要点：`markDirty` → Projection sync → `compositeRegion` → `update`；视口优先与时间预算。

**Q7. Compositor 能改图层吗？**  
要点：不能；只读消费者。

**Q8. ViewPort 是什么？**  
要点：工具与 UI 解耦的视口抽象；生产实现 `CanvasView`。

---

## D. 工具与撤销

**Q9. ToolManager 的职责边界？**  
要点：注册、切换、分发事件；不拥有文档真相。

**Q10. 为什么先 pushUndo 再写像素？**  
要点：快照必须是修改前状态；否则撤销无效或需复杂 diff。

---

## E. 选区与路径

**Q11. 路径和选区区别？**  
要点：矢量 vs mask；可互转；路径有 PathStack/专用 undo。

**Q12. 钢笔你做了哪些阶段？**  
要点：按 P0–P4 对应几何/栅格/工具/栈与面板/锚点与选区互转 + v4 IO（只谈自己参与的）。

**Q13. 为何 ProjectFormat 不兼容旧档？**  
要点：政策只认 Current；降低维护面；升版显式失败优于静默坏档。

---

## F. 滤镜 / GPU / 测试

**Q14. GPU 是必须还是可选？**  
要点：可选加速；`warmUp` 异步；失败 CPU。

**Q15. 如何证明改动没回归？**  
要点：`psdemo_tests` 无 GUI；`opsInit`；按用例名过滤；对照 tests.md。

---

## 反问面试官（可选）

- 团队图像管线是全图还是 tile/ROI？  
- 撤销是命令式还是快照式？内存上限怎么做？  
- 设备无 GPU 时的产品策略？
