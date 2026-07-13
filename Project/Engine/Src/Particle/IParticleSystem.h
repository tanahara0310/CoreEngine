#pragma once

#include <string>
#include "Math/MathCore.h"
#include "Graphics/Pipeline/PipelineStateManager.h"      // BlendMode
#include "Particle/Core/ParticleRenderDataBuilder.h"     // BillboardType

namespace CoreEngine
{
// 前方宣言
class DirectXCommon;
class ResourceFactory;
class MainModule;
class EmissionModule;
class ShapeModule;
class VelocityModule;
class ColorModule;
class ForceModule;
class SizeModule;
class RotationModule;
class NoiseModule;

/// @brief パーティクルシステムのバックエンド種別
enum class ParticleBackend {
    CPU,    ///< ParticleSystem（CPU更新。少数・細かい制御向け）
    GPU,    ///< GpuParticleSystem（ComputeShader更新。大量粒子向け）
};

/// @brief CPU / GPU パーティクルシステムの共通インターフェース
/// @details 両実装はモジュール（パラメータ定義・ImGui）を共有しており、
///          プリセットの保存/読み込みやシーンからの操作をバックエンド非依存で行える。
///          生成は BaseScene::CreateParticleSystem(ParticleBackend, ...) を使う。
///          設計は Docs/Engine/Particle/GpuParticleSystem.md 参照。
class IParticleSystem {
public:
    virtual ~IParticleSystem() = default;

    /// @brief 初期化
    virtual void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory, const std::string& name) = 0;

    // ===== 再生制御 =====
    virtual void Play() = 0;
    virtual void Stop() = 0;
    virtual bool IsPlaying() const = 0;

    // ===== 見た目 =====
    virtual void SetTexture(const std::string& texturePath) = 0;
    virtual void SetBillboardType(BillboardType type) = 0;
    virtual BillboardType GetBillboardType() const = 0;
    virtual void SetBlendMode(BlendMode mode) = 0;
    virtual BlendMode GetBlendMode() const = 0;

    // ===== エミッター =====
    virtual void SetEmitterPosition(const Vector3& position) = 0;
    virtual Vector3 GetEmitterPosition() const = 0;

    // ===== モジュール（パラメータはここから編集する） =====
    virtual MainModule& GetMainModule() = 0;
    virtual EmissionModule& GetEmissionModule() = 0;
    virtual ShapeModule& GetShapeModule() = 0;
    virtual VelocityModule& GetVelocityModule() = 0;
    virtual ColorModule& GetColorModule() = 0;
    virtual ForceModule& GetForceModule() = 0;
    virtual SizeModule& GetSizeModule() = 0;
    virtual RotationModule& GetRotationModule() = 0;
    virtual NoiseModule& GetNoiseModule() = 0;
};
}
