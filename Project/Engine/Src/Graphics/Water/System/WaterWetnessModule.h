#pragma once

#include "Graphics/Water/Surface/WaterWetnessHistory.h"

namespace CoreEngine
{
    struct WaterSettings;
    struct WaterSurfaceData;

    /// @brief 水面の高さの移り変わりを覚え、岸の濡れ跡の表を作る
    class WaterWetnessModule {
    public:
        /// @brief 今の水面の高さを記録し、snapshot.wetness へ表を書く
        /// @note snapshot の範囲と水面の高さを書いた後に呼ぶ
        void Capture(const WaterSettings& settings, float deltaSeconds, WaterSurfaceData& snapshot);

        /// @brief 覚えた高さを全部忘れる（水面が無いフレーム）
        void Clear();

    private:
        WaterWetnessHistory history_{};
    };
}
