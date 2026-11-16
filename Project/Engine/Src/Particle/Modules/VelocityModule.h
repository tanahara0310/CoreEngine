#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

struct Particle;

namespace CoreEngine
{
/// @brief 初速の設定（方向とランダム範囲。VelocityModule）
struct ParticleVelocityData {
    // 速度の方向（自動的に正規化されます）
    Vector3 startSpeed = { 0.0f, 1.0f, 0.0f };

    // 方向のランダム範囲（正規化後に適用）
    Vector3 randomSpeedRange = { 1.0f, 1.0f, 1.0f };

    // 完全ランダム方向を使用するか
    bool useRandomDirection = true;
};

/// @brief パーティクルの速度モジュール
/// 注意: 初期速度の大きさはMainModuleで設定します
/// このモジュールは速度の方向のみを決定します
class VelocityModule : public ParticleModuleT<ParticleVelocityData> {
public:
    using VelocityData = ParticleVelocityData;

    VelocityModule();
    ~VelocityModule() = default;

    /// @brief パーティクルに初期速度を適用
    /// @param particle 対象のパーティクル
    void ApplyInitialVelocity(Particle& particle);

#ifdef CORE_EDITOR
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui();
#endif

private:
    /// @brief ランダムな方向ベクトルを生成
    /// @return ランダムな方向ベクトル
    Vector3 GenerateRandomDirection();
};
}
