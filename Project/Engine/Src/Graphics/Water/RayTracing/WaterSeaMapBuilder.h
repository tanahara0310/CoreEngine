#pragma once

#include <cstdint>
#include <vector>

namespace CoreEngine
{
    /// @brief 海の範囲マップの、測った値からシェーダーが読む地図を作る部分（CPU で行う）
    namespace WaterSeaMapBuilder
    {
        /// @brief 地図に書く水深・上の物までの高さの絶対値の上限 [m]
        inline constexpr float kMaxValue = 64.0f;

        /// @brief 地図づくりの入力
        struct Input {
            const float* measured = nullptr; ///< テクセルごとの (水深, 上の物までの高さ) [m]。行が Z・列が X
            uint32_t width = 0;              ///< X のテクセル数
            uint32_t height = 0;             ///< Z のテクセル数
            uint32_t conservativeRadius = 0; ///< 粗い格子用の最小値を取る正方形の窓の半径 [テクセル]
        };

        /// @brief 地図づくりの集計（ログ用）
        struct Stats {
            uint32_t seaTexels = 0;   ///< 海とつながった水のテクセル数
            uint32_t landTexels = 0;  ///< 陸や物の下のテクセル数
            uint32_t basinTexels = 0; ///< 海とつながっていない水たまりのテクセル数
            uint32_t waterBodies = 0; ///< 水のテクセルのつながった塊の数
        };

        /// @brief 地図を作る
        /// @details 水深が正のテクセルを上下左右のつながりで塊に分け、範囲の端に触れる塊と一番大きい塊を海とする。
        ///          ほかの塊（海とつながっていない水たまり）は水深を -kMaxValue・上の物までの高さを 0 にする。
        ///          粗い格子用の 2 つは、半径 conservativeRadius の正方形の窓の中の最小値
        /// @return テクセルごとの (水深, 上の物までの高さ, 粗い格子用の水深, 粗い格子用の上の物までの高さ) を半精度で並べたもの
        std::vector<uint16_t> Build(const Input& input, Stats* outStats = nullptr);
    }
}
