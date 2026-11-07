#pragma once

#include "Graphics/RayTracing/RTInstanceMask.h"

namespace CoreEngine
{
    /// @brief 物ごとに、あるレイに当たるかの設定
    enum class RTVisibility : int {
        Auto = 0, ///< ブレンドがなし（不透明）なら当たる・それ以外なら当たらない
        On,       ///< 当たる
        Off,      ///< 当たらない
    };

    /// @brief 「影を落とす」の選択肢の名前（RTVisibility の並び）
    inline constexpr const char* kRTCastShadowNames[] = { "自動", "落とす", "落とさない" };
    /// @brief 「反射・屈折に映る」の選択肢の名前（RTVisibility の並び）
    inline constexpr const char* kRTReflectedNames[] = { "自動", "映る", "映らない" };

    /// @brief 物ごとのレイトレーシングの設定（メッシュ描画とパーティクルが持つ）
    struct RTObjectSettings {
        bool enabled = true;                         ///< false なら TLAS に載せない（影・映り込み・海底の高さのどれにも出ない）
        RTVisibility castShadows = RTVisibility::Auto; ///< 影を落とすか
        RTVisibility reflected = RTVisibility::Auto;   ///< 反射・屈折に映るか

        /// @brief 設定から、レイの目的の印（kShadowCaster・kReflected）を求める
        /// @param opaque ブレンドがなしか
        UINT ResolvePurposeMask(bool opaque) const
        {
            UINT mask = 0;
            if (Resolve(castShadows, opaque)) {
                mask |= RTInstanceMask::kShadowCaster;
            }
            if (Resolve(reflected, opaque)) {
                mask |= RTInstanceMask::kReflected;
            }
            return mask;
        }

    private:
        static bool Resolve(RTVisibility visibility, bool opaque)
        {
            return visibility == RTVisibility::On || (visibility == RTVisibility::Auto && opaque);
        }
    };
}
