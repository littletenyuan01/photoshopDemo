# 架构设计（入口）

完整架构（含图与目录规划）见：

→ **[docs/architecture.md](../docs/architecture.md)**

## 一句话

参考 GIMP：`tools` / `paint(engine)` / `core(domain)` / `display(ui)` 分离；本项目用 Qt 实现瘦身版。v1 不做 PDB/GEGL/完整插件宿主。Phase 9 图引擎主路径见 [Roadmap §9](Roadmap.md)；Phase 5 中 PSD / 存储为 Web 子集已落地，完整 SO / 插件仍为 stretch。
