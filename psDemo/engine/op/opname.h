/**
 * opname.h — 算子名枚举与 id/title 表（engine/op 层）。
 *
 * OpName 为注册表与调度器主键；对照 GIMP 层模式 → "gimp:…" 字符串映射。
 */
#ifndef ENGINE_OP_OPNAME_H
#define ENGINE_OP_OPNAME_H

#include <QString>

namespace Ps {

/**
 * 算子名枚举（中央定义，对照 GIMP 层模式用 `GimpLayerMode` 枚举、
 * 再映射到 `"gimp:…"` 字符串；GEGL 本体用字符串注册，本 Demo 用枚举做类型安全主键）。
 *
 * 新增算子：先在此加枚举项，再补 opNameId / opNameTitle，最后 opsInit 注册
 * （调整类滤镜走 FilterEval，不必进 BufferOp 注册表）。
 */
enum class OpName {
    StampDab = 0,           ///< 画笔/橡皮 dab
    FloodFill,              ///< 油漆桶洪泛
    Gradient,               ///< 渐变填充
    SolidFill,              ///< 实色/透明填充
    SelectPolygon,          ///< 多边形/套索写入选区 mask
    SelectFlood,            ///< 连通域/相似色写入选区（魔棒）
    CloneStampDab,          ///< 仿制图章 dab
    FocusDab,               ///< 模糊 / 锐化 / 涂抹 dab
    ToneDab,                ///< 减淡 / 海绵 dab
    ShapeFill,              ///< 形状填充 / 描边
    FreeTransform,          ///< 自由变换（四边形透视/仿射）
    LayerMode,              ///< 图层混合色（点算子）
    // —— 调整类（FilterEval / 调整图层；对照 PS 调整图层菜单）——
    BrightnessContrast,     ///< 亮度/对比度
    Levels,                 ///< 色阶
    Curves,                 ///< 曲线（UI 先占位）
    Exposure,               ///< 曝光度
    Vibrance,               ///< 自然饱和度
    HueSaturation,          ///< 色相/饱和度
    ColorBalance,           ///< 色彩平衡
    BlackAndWhite,          ///< 黑白
    PhotoFilter,            ///< 照片滤镜
    ChannelMixer,           ///< 通道混合器（求值后置）
    ColorLookup,            ///< 颜色查找（求值后置）
    Invert,                 ///< 反相
    Posterize,              ///< 色调分离
    Threshold,              ///< 阈值
    GradientMap,            ///< 渐变映射（求值后置）
    SelectiveColor,         ///< 可选颜色（求值后置）
    BoxBlur,                ///< 盒模糊（面积算子；暂仅像素层滤镜，非调整层）
    UnsharpMask,            ///< USM 锐化（面积算子：以盒模糊为模糊级）
    FindEdges,              ///< 查找边缘（面积算子：3×3 Sobel）
    GaussianBlur,           ///< 高斯模糊（面积算子：三次盒模糊逼近）
    MedianBlur,             ///< 中值模糊（面积算子：滑窗直方图）
    Sharpen,                ///< 锐化（面积算子：3×3 线性核）
    Emboss,                 ///< 浮雕（面积算子：3×3 方向性核 + 中灰偏置）
    /**
     * 位移（**几何算子**：不是面积算子，`inputPad()==0`）。
     *
     * 【为什么单列一类】本项目其余滤镜都是「同尺寸点/邻域算子」，可以安全地在
     * **分块**求值里跑（每块取自己那块 + pad）。位移是**把像素搬到别处**：
     * 用对称外扩去逼近就得按 `max(|dx|,|dy|)` 取输入（偏移 500px 时每块要多取 276 倍），
     * 那是「正确但荒唐」；wrap（默认，PS 语义）更是**多矩形**依赖，单矩形机制表达不了。
     * 所以它走**专用路径**：在「按层求值」的地方对**整幅**栅格做映射，不参与逐块外扩。
     * 详见 docs/engine/roi.md §5「②③ 为什么后置」。
     */
    Offset,
    Maximum,                ///< 最大值（面积算子：排序统计滤波的膨胀方向）
    Minimum,                ///< 最小值（面积算子：排序统计滤波的腐蚀方向）
    CustomFilter,           ///< 自定（3×3 卷积核 + 缩放/位移；一个算子顶多个）
    HighPass,               ///< 高通保留（src - 盒模糊 + 128，保留边缘）
    MotionBlur,             ///< 运动模糊（一维盒模糊沿 0°/45°/90°）
    AddNoise,               ///< 添加杂色（点算子；噪声是 (x,y,seed) 的确定函数，须整层求值）
    // —— 批次 A（filter-roadmap.md）：点算子 / 中值组合 / 边缘锐化 ——
    Solarize,               ///< 曝光过度（点算子：v>128 ? 255-v : v）
    DustAndScratches,       ///< 蒙尘与划痕（Demo：中值，|src-med|>阈值才替换）
    ReduceNoise,            ///< 减少杂色（Demo：中值 + 强度混合，非 PS 全功能）
    SharpenEdges,           ///< 锐化边缘（Sobel 幅值作 mask × 锐化）
    Count                   ///< 哨兵，勿当作真实算子
};

/** 稳定机器名，如 "ps:flood-fill"（对照 gegl "name" key / gimp:flood）。 */
QString opNameId(OpName name);

/** 可读标题（UI / 图层默认名用中文，与 PS 菜单对齐）。 */
QString opNameTitle(OpName name);

/** 是否为调整图层可用的滤镜算子（非像素绘制类）。 */
bool isAdjustmentOp(OpName name);

} // namespace Ps

#endif // ENGINE_OP_OPNAME_H
