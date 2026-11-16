#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"
#include "Math/Geometry/Shapes.h"

struct Particle;

namespace CoreEngine
{
/// @brief パーティクルへ加える力の設定（重力・風・抵抗・加速度フィールド。ForceModule）
struct ParticleForceData {
    Vector3 gravity = { 0.0f, -9.8f, 0.0f };    // 重力
    Vector3 wind = { 0.0f, 0.0f, 0.0f };        // 風力
    float drag = 0.0f;                          // 抵抗力 (0.0f - 1.0f)

    // 加速度フィールド
    Vector3 acceleration = { 0.0f, 0.0f, 0.0f };
    BoundingBox area = { {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f} };
    bool useAccelerationField = false;
};

/// @brief パーティクルの力場モジュール
class ForceModule : public ParticleModuleT<ParticleForceData> {
public:
    using ForceData = ParticleForceData;

    ForceModule();
    ~ForceModule() = default;

    /// @brief パーティクルに力を適用（gravityModifier対応）
    /// @param particle 対象のパーティクル
    /// @param deltaTime フレーム時間
    /// @param gravityModifier 重力倍率（MainModuleから取得）
    void ApplyForces(Particle& particle, float deltaTime, float gravityModifier);

#ifdef CORE_EDITOR
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui();
#endif
};
}
