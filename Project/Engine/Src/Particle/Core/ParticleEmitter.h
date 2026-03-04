#pragma once

#include <list>
#include <cstdint>
#include "Math/MathCore.h"

namespace CoreEngine
{
// 前方宣言
struct Particle;
struct EulerTransform;
class MainModule;
class EmissionModule;
class ShapeModule;
class VelocityModule;
class RotationModule;

/// @brief パーティクル生成・放出クラス
/// パーティクルの初期化と放出を担当
class ParticleEmitter {
public:
    ParticleEmitter() = default;
    ~ParticleEmitter() = default;

    /// @brief 初期化（モジュールへの参照を設定）
    /// @param mainModule メインモジュール
    /// @param emissionModule 放出モジュール
    /// @param shapeModule 形状モジュール
    /// @param velocityModule 速度モジュール
    /// @param rotationModule 回転モジュール
    void Initialize(
        MainModule* mainModule,
        EmissionModule* emissionModule,
        ShapeModule* shapeModule,
        VelocityModule* velocityModule,
        RotationModule* rotationModule
    );

    /// @brief パーティクルを放出
    /// @param count 放出数
    /// @param emitterTransform エミッターのトランスフォーム
    /// @param maxParticles 最大パーティクル数
    /// @param outParticles 出力先のパーティクルリスト
    /// @return 実際に放出されたパーティクル数
    uint32_t EmitParticles(
        uint32_t count,
        const EulerTransform& emitterTransform,
        uint32_t maxParticles,
        std::list<Particle>& outParticles
    );

private:
    /// @brief 新しいパーティクルを生成
    /// @param emitterTransform エミッターのトランスフォーム
    /// @return 生成されたパーティクル
    Particle CreateParticle(const EulerTransform& emitterTransform);

    // モジュールへの参照（ポインタ）
    MainModule* mainModule_ = nullptr;
    EmissionModule* emissionModule_ = nullptr;
    ShapeModule* shapeModule_ = nullptr;
    VelocityModule* velocityModule_ = nullptr;
    RotationModule* rotationModule_ = nullptr;
};
}
