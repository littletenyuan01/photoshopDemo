/**
 * tilesfilterop.h — 拼贴（网格切块 + 确定性随机偏移）。
 */
#ifndef ENGINE_OP_TILESFILTEROP_H
#define ENGINE_OP_TILESFILTEROP_H

#include "filterop.h"

namespace Ps {

class TilesFilterOp : public FilterOp
{
public:
    OpName name() const override { return OpName::Tiles; }
    bool prepare(const FilterContext &ctx) override;
    void process(QImage &image, const FilterContext &ctx) override;
    void finish(const FilterContext &ctx) override;

private:
    int m_count = 10;       ///< 每边块数 2..100
    int m_maxOffset = 10;   ///< 最大偏移 %（相对块边）
    int m_fill = 0;         ///< 0 透明 / 1 反相 / 2 原图
    quint32 m_seed = 0xC0FFEE01u;
};

} // namespace Ps

#endif
