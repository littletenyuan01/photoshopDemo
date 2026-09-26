# 生成面板图标的 SVG 矢量源文件（替代早先的像素手绘）。
#
# 为什么换成 SVG：
# 早先是 48x48 PNG 逐像素画，显示时缩到 22px（底栏按钮）会发糊，且无抗锯齿。
# SVG 是矢量，由 Qt 在**目标尺寸**上直接光栅化，任意尺寸、任意 DPI 都锐利。
#
# 设计约束（Feather 风格）：
#   viewBox 24x24，描边 2，round cap/join，主色 #DCDCDC，次级色 #8C8C8C
#   所有坐标在 24 网格内（内容大致 3..21）
#
# 运行：python _gen_svg_icons.py   生成/覆盖 24 个 .svg
import os

OUT = os.path.dirname(os.path.abspath(__file__))       # .../icons/layers
ROOT = os.path.dirname(OUT)                             # .../icons

_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
         'viewBox="0 0 24 24" fill="none" stroke="#DCDCDC" stroke-width="2.2" '
         'stroke-linecap="round" stroke-linejoin="round">')


def svg(*inner, extra_attrs=""):
    return _HEAD + "".join(inner) + "</svg>"


# ---------- 图形元件（供"互转"类图标复用） ----------
# 原则：**只放两个元件，靠"轮廓"区分，左=源、右=目标**。
# 试过加箭头，但 22px 下箭头太小反而糊成一团（实测）；也试过两个都是方框，
# 结果虚线框和竖条分不出来。最终按**轮廓差异**设计：
#   选区 = 虚线圆（圆形轮廓）· 通道 = 竖条（竖向长条）· 路径 = 斜线+锚点（斜向）
_LEFT = 6.0     # 左（源）元件中心
_RIGHT = 18.0   # 右（目标）元件中心


def _marquee(cx):
    """虚线圆 = 选区（PS 的蚂蚁线轮廓印象）。"""
    return f'<circle cx="{cx}" cy="12" r="5.4" stroke-dasharray="2.2 1.8"/>'


def _channel_bar(cx):
    """竖条 + 中缝 = 通道。"""
    return (f'<rect x="{cx - 4}" y="4.5" width="8" height="15" rx="1.4"/>'
            f'<path d="M{cx} 4.5v15"/>')


def _anchor_path(cx):
    """斜线 + 两端锚点方块 = 路径。"""
    x0, y0, x1, y1 = cx - 3.5, 16.5, cx + 3.5, 7.5
    return (f'<path d="M{x0} {y0}L{x1} {y1}"/>'
            f'<rect x="{x0 - 1.4}" y="{y0 - 1.4}" width="2.8" height="2.8" rx="0.4" '
            f'fill="#DCDCDC" stroke="none"/>'
            f'<rect x="{x1 - 1.4}" y="{y1 - 1.4}" width="2.8" height="2.8" rx="0.4" '
            f'fill="#DCDCDC" stroke="none"/>')


def _plus(cx=19.5, cy=20.0, arm=2.5):
    """右下角加号（表示"新建"）。"""
    return (f'<path d="M{cx} {cy - arm}v{arm * 2}"/>'
            f'<path d="M{cx - arm} {cy}h{arm * 2}"/>')


# name -> (子目录, 文件名, SVG 内容)
ICONS = {
    # ---------- 图层面板底栏 ----------
    "link": ("layers", "link", svg(
        '<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/>',
        '<path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>')),
    "fx": ("layers", "fx", svg(
        '<path d="M9 6.5v11"/>',
        '<path d="M9 6.5h4.6"/>',
        '<path d="M9 12h3.2"/>',
        '<path d="M15.4 8l4.4 7.5"/>',
        '<path d="M19.8 8l-4.4 7.5"/>')),
    "mask": ("layers", "mask", svg(
        '<rect x="4" y="4" width="16" height="16" rx="2"/>',
        '<circle cx="12" cy="12" r="3.4"/>')),
    "adjustment": ("layers", "adjustment", svg(
        '<circle cx="12" cy="12" r="8.6"/>',
        '<path d="M12 3.4a8.6 8.6 0 0 1 0 17.2Z" fill="#DCDCDC" stroke="none"/>')),
    "group": ("layers", "group", svg(
        '<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>')),
    "new-layer": ("layers", "new-layer", svg(
        '<path d="M14 3H6a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V8z"/>',
        '<path d="M14 3v5h5"/>',
        '<path d="M12 12v5"/>',
        '<path d="M9.5 14.5h5"/>')),
    "delete": ("layers", "delete", svg(
        '<path d="M3 6h18"/>',
        '<path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/>',
        '<path d="M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2"/>',
        '<path d="M10 11v6"/>',
        '<path d="M14 11v6"/>')),

    # ---------- 类型筛选行（顺序对齐 PS） ----------
    "filter-pixel": ("layers", "filter-pixel", svg(
        '<rect x="3.5" y="3.5" width="17" height="17" rx="2"/>',
        '<path d="M6.5 6.5h4.5v4.5H6.5Z" fill="#DCDCDC" stroke="none"/>',
        '<path d="M13 13h4.5v4.5H13Z" fill="#DCDCDC" stroke="none"/>')),
    "filter-adjust": ("layers", "filter-adjust", svg(
        '<circle cx="12" cy="12" r="8.6"/>',
        '<path d="M12 3.4a8.6 8.6 0 0 1 0 17.2Z" fill="#DCDCDC" stroke="none"/>')),
    "filter-type": ("layers", "filter-type", svg(
        '<path d="M5 6h14"/>',
        '<path d="M12 6v12"/>',
        '<path d="M8.5 18h7"/>')),
    "filter-shape": ("layers", "filter-shape", svg(
        '<rect x="3.5" y="3.5" width="10" height="10" rx="1.5"/>',
        '<circle cx="16" cy="16" r="5"/>')),
    "filter-smart": ("layers", "filter-smart", svg(
        '<path d="M4 4h10l6 6v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1Z"/>',
        '<path d="M14 4v6h6"/>',
        '<rect x="8" y="12" width="8" height="5" rx="0.5" fill="#DCDCDC" stroke="none"/>')),

    # ---------- 锁定行（PS 四种锁） ----------
    "lock-transparent": ("layers", "lock-transparent", svg(
        '<rect x="2.5" y="2.5" width="19" height="19" rx="2"/>',
        '<path d="M5.5 5.5h4.5v4.5H5.5Z" fill="#8C8C8C" stroke="none"/>',
        '<path d="M14 14h4.5v4.5H14Z" fill="#8C8C8C" stroke="none"/>',
        # 锁：先深色底再浅色面，形成描边，叠在棋盘上仍清晰
        '<path d="M9.6 9V7a2.4 2.4 0 0 1 4.8 0v2" stroke="#2b2b2b" stroke-width="4.2"/>',
        '<path d="M9.6 9V7a2.4 2.4 0 0 1 4.8 0v2"/>',
        '<rect x="7.6" y="9" width="8.8" height="6.8" rx="1.3" fill="#2b2b2b"/>',
        '<rect x="8.3" y="9.7" width="7.4" height="5.4" rx="1" fill="#DCDCDC" stroke="none"/>')),
    "lock-image": ("layers", "lock-image", svg(
        '<rect x="2.5" y="2.5" width="19" height="19" rx="2"/>',
        '<circle cx="16" cy="6.6" r="1.5" fill="#8C8C8C" stroke="none"/>',
        '<path d="M5 14.5l3.5-3.5 2.5 2 2-2 4 3.5" stroke="#8C8C8C" stroke-width="1.6"/>',
        '<path d="M9.6 15.5v-2a2.4 2.4 0 0 1 4.8 0v2" stroke="#2b2b2b" stroke-width="4.2"/>',
        '<path d="M9.6 15.5v-2a2.4 2.4 0 0 1 4.8 0v2"/>',
        '<rect x="7.6" y="15.5" width="8.8" height="5.8" rx="1.3" fill="#2b2b2b"/>',
        '<rect x="8.3" y="16" width="7.4" height="4.8" rx="1" fill="#DCDCDC" stroke="none"/>')),
    "lock-position": ("layers", "lock-position", svg(
        '<path d="M12 3v18"/>',
        '<path d="M3 12h18"/>',
        '<path d="M12 3l-2.5 2.5"/><path d="M12 3l2.5 2.5"/>',
        '<path d="M12 21l-2.5-2.5"/><path d="M12 21l2.5-2.5"/>',
        '<path d="M3 12l2.5-2.5"/><path d="M3 12l2.5 2.5"/>',
        '<path d="M21 12l-2.5-2.5"/><path d="M21 12l-2.5 2.5"/>')),
    "lock-all": ("layers", "lock-all", svg(
        '<path d="M9 10V8a3 3 0 0 1 6 0v2" fill="#DCDCDC" stroke="none"/>',
        '<rect x="6" y="10" width="12" height="9" rx="1.6" fill="#DCDCDC" stroke="none"/>')),

    # ---------- 通道面板底栏 ----------
    "load-selection": ("channels", "load-selection", svg(
        '<circle cx="8" cy="12" r="5.5" stroke-dasharray="3 2.4"/>',
        '<path d="M16 12h5"/>',
        '<path d="M18 9l3 3-3 3"/>')),
    "save-selection": ("channels", "save-selection", svg(
        '<path d="M8 12H3"/>',
        '<path d="M6 9l-3 3 3 3"/>',
        '<rect x="12" y="8" width="8" height="8" rx="1" fill="#DCDCDC" stroke="none"/>')),
    # ---------- 通道面板底栏 ----------
    # 互转：左=源、右=目标（靠轮廓区分：竖条 / 虚线圆）
    "load-selection": ("channels", "load-selection", svg(
        _channel_bar(_LEFT), _marquee(_RIGHT))),
    "save-selection": ("channels", "save-selection", svg(
        _marquee(_LEFT), _channel_bar(_RIGHT))),
    "new-channel": ("channels", "new-channel", svg(
        '<rect x="2.5" y="4.5" width="13" height="15" rx="1.5"/>',
        '<path d="M9 4.5v15"/>',
        _plus())),

    # ---------- 路径面板底栏 ----------
    # fill / stroke 用**同一个三角**：实心 vs 空心，一眼分得出"填充/描边"
    "fill-path": ("paths", "fill", svg(
        '<path d="M4.5 18.5L12 5l7.5 13.5Z" fill="#DCDCDC" stroke="none"/>')),
    "stroke-path": ("paths", "stroke", svg(
        '<path d="M4.5 18.5L12 5l7.5 13.5Z" stroke-width="2.4"/>')),
    # 互转：左=源、右=目标（靠轮廓区分：斜线锚点 / 虚线圆）
    "path-to-selection": ("paths", "to-selection", svg(
        _anchor_path(_LEFT), _marquee(_RIGHT))),
    "selection-to-path": ("paths", "from-selection", svg(
        _marquee(_LEFT), _anchor_path(_RIGHT))),
    "new-path": ("paths", "new-path", svg(
        '<path d="M9 16l6-6 2.5 2.5-6 6z"/>',
        '<path d="M15 10L13.6 3.5 2.5 2.5 4 13.5 10.5 15z"/>',
        '<circle cx="8.5" cy="8.5" r="1.7"/>',
        _plus())),

    # ---------- 面板通用（属性 / 色板面板底栏与「对齐并分布」用） ----------
    "plus": ("ui", "plus", svg(
        '<path d="M12 5v14"/>',
        '<path d="M5 12h14"/>')),
    # 「对齐并分布」：一条参考线 + 两个待对齐方块（GIMP 用 gimpalignoptions，图标自绘）
    "align-left": ("ui", "align-left", svg(
        '<path d="M4 3v18"/>',
        '<rect x="8" y="6" width="12" height="5" rx="0.6"/>',
        '<rect x="8" y="13" width="7" height="5" rx="0.6"/>')),
    "align-hcenter": ("ui", "align-hcenter", svg(
        '<path d="M12 3v18"/>',
        '<rect x="5" y="6" width="14" height="5" rx="0.6"/>',
        '<rect x="8" y="13" width="8" height="5" rx="0.6"/>')),
    "align-right": ("ui", "align-right", svg(
        '<path d="M20 3v18"/>',
        '<rect x="4" y="6" width="12" height="5" rx="0.6"/>',
        '<rect x="9" y="13" width="7" height="5" rx="0.6"/>')),
    "align-top": ("ui", "align-top", svg(
        '<path d="M3 4h18"/>',
        '<rect x="6" y="8" width="5" height="12" rx="0.6"/>',
        '<rect x="13" y="8" width="5" height="7" rx="0.6"/>')),
    "align-vcenter": ("ui", "align-vcenter", svg(
        '<path d="M3 12h18"/>',
        '<rect x="6" y="5" width="5" height="14" rx="0.6"/>',
        '<rect x="13" y="8" width="5" height="8" rx="0.6"/>')),
    "align-bottom": ("ui", "align-bottom", svg(
        '<path d="M3 20h18"/>',
        '<rect x="6" y="4" width="5" height="12" rx="0.6"/>',
        '<rect x="13" y="9" width="5" height="7" rx="0.6"/>')),
}


def main():
    written = 0
    for name, (sub, fname, content) in ICONS.items():
        d = os.path.join(ROOT, sub)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, fname + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        written += 1
        print("wrote", os.path.relpath(path, ROOT))
    print("共", written, "个 SVG")


if __name__ == "__main__":
    main()
