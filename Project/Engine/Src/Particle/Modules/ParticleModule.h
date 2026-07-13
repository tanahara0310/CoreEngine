#pragma once

#include "Math/MathCore.h"
#include "Utility/Random/RandomGenerator.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
// 前方宣言
struct Particle;

/// @brief パーティクルモジュールの基底クラス
class ParticleModule {
public:
    ParticleModule() = default;
    virtual ~ParticleModule() = default;

    /// @brief モジュールの有効/無効を設定
    /// @param enabled 有効にするかどうか
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// @brief モジュールが有効かどうか
    /// @return 有効な場合true
    bool IsEnabled() const { return enabled_; }

    /// @brief GPUバックエンドで動作しているかを設定
    /// @note GpuParticleSystem::Initialize が true を設定する。
    ///       ImGui側でCPU専用項目（形状のデバッグ描画など）を隠すために使う。
    void SetGpuBackend(bool isGpu) { gpuBackend_ = isGpu; }

    /// @brief GPUバックエンドで動作しているか
    bool IsGpuBackend() const { return gpuBackend_; }

#ifdef USE_IMGUI
    /// @brief ImGuiデバッグ表示（純粋仮想関数）
    /// @return UIに変更があった場合true
    virtual bool ShowImGui() = 0;
#endif

protected:
    bool enabled_ = true;
    bool gpuBackend_ = false;  // GPUバックエンド上で動作しているか（UI出し分け用）
    RandomGenerator& random_ = RandomGenerator::GetInstance(); // 統一乱数生成器への参照

    /// @brief ランダム性を適用したヘルパー関数
    /// @param baseValue 基本値
    /// @param randomness ランダム性の範囲
    /// @return ランダム性を適用した値
    float ApplyRandomness(float baseValue, float randomness) {
        if (randomness <= 0.0f) return baseValue;
        return baseValue + random_.GetFloat(-randomness, randomness);
    }
};
}
