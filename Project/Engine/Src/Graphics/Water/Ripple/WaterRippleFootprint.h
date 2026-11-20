#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Graphics/Water/Query/WaterQuery.h"
#include "Physics/DisplacementCells.h"

namespace CoreEngine
{
    class WaterRippleSimulation;

    /// @brief 物が押しのけている水の形を覚え、前に入れた形からの変化を波紋の源として入れる
    /// @details 形を分けた 8 つの塊を上下の 2 つずつ組にした 4 本の柱で表す。柱ごとに、波紋を除いた水面より下にある高さ
    ///          （押しのけている深さ）を求め、柱の広さと同じ体積の盛り上がりにして、前に入れた盛り上がりを引いて今の盛り上がりを足す。
    ///          止まって浮いている物は何も入れず、進む・揺れる・沈む・浮き上がる分だけが波紋になる。
    ///          水面の結果が届いていない塊がある間と、大きく飛んだフレームは、形を覚え直すだけで何も入れない。
    class WaterRippleFootprint {
    public:
        /// @brief 柱の数
        static constexpr uint32_t kColumnCount = 4;
        /// @brief 1 フレームでこれより大きく動いた柱は置き直したとみなす [m]
        static constexpr float kTeleportMeters = 1.0f;

        /// @brief 塊 1 つの今の置き場所と、その真上・真下の水面
        struct CellState {
            PlacedDisplacementCell placed{};
            WaterSample water{};
        };

        /// @brief 塊から柱の押しのけを求め、前に入れた形との差を源として足す（フレームに 1 回）
        /// @param strength 押しのけに掛ける倍率（0 なら覚えた形を捨てる）
        void Emit(WaterRippleSimulation& simulation, std::span<const CellState, kDisplacementCellCount> cells, float strength);

        /// @brief 覚えた形を捨てる（次の Emit は形を覚えるだけで何も入れない）
        void Reset() { hasPrevious_ = false; }

        /// @brief 最後に覚えた形が押しのけている水の体積 [m³]（倍率を掛ける前）
        float GetDisplacedVolume() const { return hasPrevious_ ? displacedVolume_ : 0.0f; }

    private:
        struct Column {
            float x = 0.0f;
            float y = 0.0f;      ///< 柱の中心の高さ [m]
            float z = 0.0f;
            float radius = 0.0f; ///< 盛り上がりの半径 [m]（ガウスの σ の 2 倍）
            float depth = 0.0f;  ///< 押しのけている深さ [m]（倍率込み）
        };

        std::array<Column, kColumnCount> previous_{};
        float displacedVolume_ = 0.0f;
        bool hasPrevious_ = false;
    };
}
