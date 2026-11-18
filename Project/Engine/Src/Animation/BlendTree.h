#pragma once

#include "Animation/AnimatorController.h"
#include "Math/Vector/Vector2.h"

#include <span>
#include <string>

/// @brief ブレンドツリーの子の重み
namespace CoreEngine::BlendTree
{
    /// @brief 1D：値をはさむ隣の 2 本を直線で混ぜる重み
    /// @details 境の値は並んでいなくてよい。値が端より外なら端の 1 本だけ。同じ境の値が並ぶときは前の 1 本。
    /// @param weights thresholds と同じ数（足して 1。子が無ければ何も書かない）
    void Compute1D(std::span<const float> thresholds, float value, std::span<float> weights);

    /// @brief 2D：平面に置いた子を Freeform Directional（極座標の勾配帯）で混ぜる重み
    /// @details 原点からの長さの差と向きの角度の差で、子の組ごとに値の近さを測る。原点に置いた子は、
    ///          向きを値の向きと同じとみなす。子の位置ちょうどの値ならその子だけが 1 になる。
    /// @param weights positions と同じ数（足して 1。子が無ければ何も書かない）
    void Compute2D(std::span<const Vector2> positions, const Vector2& value, std::span<float> weights);

    /// @brief 状態がパラメータの初めの値のときにいちばん重い子のクリップ（クリップの状態ならそのクリップ）
    /// @return 無ければ空
    std::string PrimaryClip(const AnimatorControllerData& controller, const AnimatorState& state);

    /// @brief 状態がそのクリップを使っているか（ブレンドツリーの子も見る）
    bool UsesClip(const AnimatorState& state, const std::string& clipId);
}
