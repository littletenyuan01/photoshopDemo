# 架构设计（入口）

完整架构（含图与目录规划）见：

→ **[docs/architecture.md](../docs/architecture.md)**

## 一句话

参考 GIMP：`tools` / `paint(engine)` / `core(domain)` / `display(ui)` 分离；本项目用 Qt 实现瘦身版。v1 不做 PDB/GEGL/完整插件宿主；PSD / 智能对象 / 极简插件库仅作有余力 stretch（见 [Roadmap](Roadmap.md) Phase 5）。
