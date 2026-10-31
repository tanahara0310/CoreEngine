#pragma once

#include <cstddef>
#include <vector>

#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    /// @brief 水面の高さの移り変わりから、高さごとに最後に水に浸かった時刻を覚え、濡れ跡の表を作る
    /// @details 水面より上の高さ y の面の濡れ具合は exp(−(今 − 最後に水面が y 以上だった時刻) / 乾く時間)。
    ///          水面の高さは範囲のどこでも同じなので、濡れ具合は高さだけで決まる
    class WaterWetnessHistory {
    public:
        /// @brief 今の水面の高さを覚える（フレームに 1 回）
        /// @param drySeconds 濡れ具合が 1/e になるまでの時間 [s]（乾ききった高さを忘れるのに使う）
        void Record(float waterHeight, float deltaSeconds, float drySeconds);

        /// @brief 覚えた高さを全部忘れる
        void Clear();

        /// @brief 今の水面から上の濡れ跡の表を作る（何も覚えていなければ enabled = 0）
        WaterWetnessBinding BuildBinding(float drySeconds) const;

    private:
        /// @brief 水面がこの高さにあった時刻
        struct Mark {
            float height = 0.0f;
            double time = 0.0;
        };

        /// @brief 水面が height 以上だった最後の時刻
        /// @param upper height 以上の印の番号。次の印は height 以下で、upper の印より低い
        double LastSubmergedTime(size_t upper, float height) const;

        /// @brief height より高い印のうち、いちばん後ろの番号（height より高い印が無ければ marks_.size()）
        size_t FindUpperMark(float height) const;

        /// @brief 高い順（古い順）に並べた印。最後の印が今の水面
        std::vector<Mark> marks_;
        double now_ = 0.0;
    };
}
