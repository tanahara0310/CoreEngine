#pragma once

#include <string>
#include "Math/MathCore.h"
#include "Graphics/Pipeline/PipelineStateManager.h"      // BlendMode
#include "Particle/Core/ParticleRenderDataBuilder.h"     // BillboardType

namespace CoreEngine
{
// 前方宣言
class GraphicsCore;
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
    virtual void Initialize(GraphicsCore* dxCommon, ResourceFactory* resourceFactory, const std::string& name) = 0;

    // ===== 再生制御 =====
    /// @brief 再生を開始する
    virtual void Play() = 0;
    /// @brief 再生を停止する（生存中のパーティクルも消える）
    virtual void Stop() = 0;
    /// @brief 再生中か
    virtual bool IsPlaying() const = 0;

    // ===== 見た目 =====
    /// @brief パーティクルに貼るテクスチャを設定
    virtual void SetTexture(const std::string& texturePath) = 0;
    /// @brief ビルボードの向き方を設定
    virtual void SetBillboardType(BillboardType type) = 0;
    /// @brief ビルボードの向き方を取得
    virtual BillboardType GetBillboardType() const = 0;
    /// @brief ブレンドモードを設定
    virtual void SetBlendMode(BlendMode mode) = 0;
    /// @brief ブレンドモードを取得
    virtual BlendMode GetBlendMode() const = 0;

    // ===== エミッター =====
    /// @brief エミッターのワールド座標を設定
    virtual void SetEmitterPosition(const Vector3& position) = 0;
    /// @brief エミッターのワールド座標を取得
    virtual Vector3 GetEmitterPosition() const = 0;

    // ===== モジュール（パラメータはここから編集する） =====
    /// @brief 寿命・初期色などの基本モジュールを取得
    virtual MainModule& GetMainModule() = 0;
    /// @brief 放出レートモジュールを取得
    virtual EmissionModule& GetEmissionModule() = 0;
    /// @brief 放出形状モジュールを取得
    virtual ShapeModule& GetShapeModule() = 0;
    /// @brief 初速モジュールを取得
    virtual VelocityModule& GetVelocityModule() = 0;
    /// @brief 色モジュールを取得
    virtual ColorModule& GetColorModule() = 0;
    /// @brief 力場モジュールを取得
    virtual ForceModule& GetForceModule() = 0;
    /// @brief サイズモジュールを取得
    virtual SizeModule& GetSizeModule() = 0;
    /// @brief 回転モジュールを取得
    virtual RotationModule& GetRotationModule() = 0;
    /// @brief ノイズ（乱流）モジュールを取得
    virtual NoiseModule& GetNoiseModule() = 0;
};
}
