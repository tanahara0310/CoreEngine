#pragma once

#include "Math/MathCore.h"
#include "Utility/Random/RandomGenerator.h"

#ifdef CORE_EDITOR
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
    /// @note GpuParticleSystemComponent のコンストラクタが true を設定する。
    ///       ImGui側でCPU専用項目（形状のデバッグ描画など）を隠すために使う。
    void SetGpuBackend(bool isGpu) { gpuBackend_ = isGpu; }

    /// @brief GPUバックエンドで動作しているか
    bool IsGpuBackend() const { return gpuBackend_; }

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

/// @brief 設定の構造体を 1 つ持つパーティクルモジュールの基底
/// @tparam TData 設定の構造体
template <class TData>
class ParticleModuleT : public ParticleModule {
public:
    using Data = TData;

    /// @brief 設定を差し替える
    /// @param data 設定
    void SetData(const TData& data) { data_ = data; }

    /// @brief 設定を取得
    /// @return 設定の参照
    const TData& GetData() const { return data_; }

    /// @brief 設定を取得（書き換え可能）
    /// @return 設定の参照
    TData& GetData() { return data_; }

protected:
    TData data_;
};
}
