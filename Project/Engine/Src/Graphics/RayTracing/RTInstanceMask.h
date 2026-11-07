#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief TLAS のインスタンスの印（InstanceMask のビット）
    /// @details レイは当てたい印の OR を InstanceInclusionMask に渡す。印が 1 つでも重なる物に当たる。
    ///          HLSL 側 Include/RayTracing/RTInstanceMask.hlsli の kRTInstanceMask* と 1:1。
    ///          kShadowCaster と kReflected は物ごとの設定（RTObjectSettings）で付け外しする。
    ///          kStaticSolid と kMovingSolid は形の決まった不透明な物にだけ付き、海底の高さなど「地面」を調べるレイが使う。
    namespace RTInstanceMask {
        inline constexpr UINT kShadowCaster = 0x01; ///< 影を落とす物（光が届くかを調べるレイが当たる）
        inline constexpr UINT kReflected = 0x02;    ///< 反射・屈折に映る物（当たった面の色を求めるレイが当たる）
        inline constexpr UINT kStaticSolid = 0x04;  ///< 止まっている形の決まった不透明な物（地形・岩・建物など）
        inline constexpr UINT kMovingSolid = 0x08;  ///< 動いている形の決まった不透明な物（スキニングと、少し前まで動いていた物）
        inline constexpr UINT kAll = 0xFF;
    }
}
