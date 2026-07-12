#pragma once

#include "Particle/ParticleSystem.h"
#include "Math/MathCore.h"
#include <string>

namespace CoreEngine
{
class DirectXCommon;
class ResourceFactory;

/// @brief circle.png を使ったヒットエフェクト
/// スケールを縦長にしてランダムなZ回転を与えることでヒット感を演出する
class HitEffect {
public:
    HitEffect() = default;
    ~HitEffect() = default;

    /// @brief 初期化
    /// @param particleSystem 呼び出し元シーンで生成済みの ParticleSystem（所有権は持たない）
    /// @param dxCommon       DirectXCommon
    /// @param factory        ResourceFactory
    /// @param texturePath    circle.png などのテクスチャパス
    void Initialize(ParticleSystem* particleSystem,
                    DirectXCommon* dxCommon,
                    ResourceFactory* factory,
                    const std::string& texturePath = "Application/Assets/Textures/circle.png");

    /// @brief 指定位置でヒットエフェクトを再生する
    /// @param position ワールド空間の再生位置
    void Play(const Vector3& position);

    /// @brief 再生中か
    bool IsPlaying() const;

private:
    ParticleSystem* particleSystem_ = nullptr;

    /// @brief パーティクルシステムのパラメータをヒットエフェクト向けに設定する
    void SetupHitEffectParameters();
};

} // namespace CoreEngine
