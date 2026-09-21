#ifndef BLENDMODE_H
#define BLENDMODE_H

namespace Ps {

/**
 * 图层混合模式。
 * v1 仅实现 Normal；枚举先占位，避免以后改 API。
 * 合成逻辑见 engine/Compositor。
 */
enum class BlendMode {
    Normal = 0
    // 后续：Multiply / Screen / Overlay ...
};

} // namespace Ps

#endif // BLENDMODE_H
