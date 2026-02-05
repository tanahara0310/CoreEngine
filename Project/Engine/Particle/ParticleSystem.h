#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>
#include <vector>
#include <list>
#include <string>

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include "Debug/ParticleSystemDebugUI.h"
#endif

// Math
#include "MathCore.h"

// Graphics関連
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/PipelineStateManager.h"

// GameObject基底クラス
#include "Engine/ObjectCommon/GameObject.h"

// モジュール関連
#include "Modules/MainModule.h"
#include "Modules/EmissionModule.h"
#include "Modules/ShapeModule.h"
#include "Modules/VelocityModule.h"
#include "Modules/ColorModule.h"
#include "Modules/ForceModule.h"
#include "Modules/SizeModule.h"
#include "Modules/RotationModule.h"
#include "Modules/NoiseModule.h"

// Core関連
#include "Core/ParticleResourceManager.h"
#include "Core/ParticleRenderDataBuilder.h"
#include "Core/ParticleUpdater.h"
#include "Core/ParticleEmitter.h"

// プリセット管理
#include "ParticlePresetManager.h"

// 前方宣言
class ICamera;
class ModelResource;

// パーティクルのパラメータ

namespace CoreEngine
{
struct Particle {
    EulerTransform transform;
    Vector3 velocity;
    Vector4 color;
    Vector4 initialColor;  // MainModuleから設定された初期色（グラデーション用）
    Vector3 initialScale;  // MainModuleから設定された初期サイズ（サイズ変化用）
    float lifeTime;
    float currentTime;
    Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f };
};

/// @brief パーティクルシステムクラス
class ParticleSystem : public GameObject {
public:
    // GPU用の絶対的な最大インスタンス数（メモリ確保用）
    // 実際の最大パーティクル数はMainModule.maxParticlesで制御
    static constexpr uint32_t kNumMaxInstance = 1028; // パーティクルの最大数

    ParticleSystem() = default;
    ~ParticleSystem() override = default;

    /// @brief 初期化
    /// @param dxCommon DirectXCommon
    /// @param resourceFactory リソースファクトリ
    /// @param name オブジェクト名（ImGui表示用、省略可）
    void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory, const std::string& name = "");

    /// @brief 更新処理（他のオブジェクトと統一）
    void Update() override;

    /// @brief 描画（3D専用 - カメラ必須、Object3dと同じインターフェース）
    /// @param camera カメラオブジェクト
    void Draw(const ICamera* camera) override;

    // ──────────────────────────────────────────────────────────
    // GameObjectインターフェース実装
    // ──────────────────────────────────────────────────────────

    /// @brief 描画パスタイプを取得
    RenderPassType GetRenderPassType() const override {
        return renderMode_ == ParticleRenderMode::Model ? RenderPassType::ModelParticle : RenderPassType::Particle;
    }

#ifdef _DEBUG
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "ParticleSystem"; }

    /// @brief ImGuiデバッグUI描画（ParticleSystem独自実装）
    bool DrawImGui() override;
#endif

    /// @brief ブレンドモードを設定
    /// @param mode ブレンドモード
    void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }

    /// @brief ブレンドモードを取得
    /// @return ブレンドモード
    BlendMode GetBlendMode() const override { return blendMode_; }

    // ──────────────────────────────────────────────────────────
    // パーティクルシステム制御
    // ──────────────────────────────────────────────────────────

    /// @brief 再生開始
    void Play();

    /// @brief 再生停止
    void Stop();

    /// @brief 再生中かどうか
    /// @return 再生中の場合true
    bool IsPlaying() const;

    /// @brief 全パーティクルクリア
    void Clear();

    /// @brief パーティクルが全て消滅したか（ワンショット用）
    /// @return パーティクルが0個かつ再生終了している場合true
    bool IsFinished() const;

    // ──────────────────────────────────────────────────────────
    // テクスチャ管理
    // ──────────────────────────────────────────────────────────

    /// @brief テクスチャを設定
    /// @param texturePath テクスチャパス
    void SetTexture(const std::string& texturePath);

    /// @brief テクスチャハンドルを取得
    /// @return テクスチャのGPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle() const { return texture_.gpuHandle; }

    // ──────────────────────────────────────────────────────────
    // モデルパーティクル管理
    // ──────────────────────────────────────────────────────────

    /// @brief モデルリソースを設定（モデルパーティクルとして使用）
    /// @param modelResource モデルリソースのポインタ
    void SetModelResource(ModelResource* modelResource);

    /// @brief モデルリソースを取得
    /// @return モデルリソースのポインタ（nullptrの場合はビルボードモード）
    ModelResource* GetModelResource() const { return modelResource_; }

    /// @brief 描画モードを取得
    /// @return 描画モード
    ParticleRenderMode GetRenderMode() const { return renderMode_; }

    /// @brief モデルパーティクルかどうか
    /// @return モデルパーティクルの場合true
    bool IsModelParticle() const { return renderMode_ == ParticleRenderMode::Model; }

    // ──────────────────────────────────────────────────────────
    // 設定アクセッサ
    // ──────────────────────────────────────────────────────────

    /// @brief エミッター位置を設定
    /// @param position 位置
    void SetEmitterPosition(const Vector3& position) { emitterTransform_.translate = position; }

    /// @brief エミッター位置を取得
    /// @return 位置
    Vector3 GetEmitterPosition() const { return emitterTransform_.translate; }

    /// @brief ビルボードタイプを設定
    /// @param type ビルボードタイプ
    void SetBillboardType(BillboardType type) { billboardType_ = type; }

    /// @brief ビルボードタイプを取得
    /// @return ビルボードタイプ
    BillboardType GetBillboardType() const { return billboardType_; }

    // ──────────────────────────────────────────────────────────
    // レンダラーがアクセスするためのゲッター
    // ──────────────────────────────────────────────────────────

    /// @brief インスタンス数を取得
    uint32_t GetInstanceCount() const { return instanceCount_; }

    /// @brief インスタンシングSRVのGPUハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingSrvHandleGPU() const {
        return resourceManager_->GetSrvHandleGPU();
    }

    // ──────────────────────────────────────────────────────────
    // モジュールアクセッサ
    // ──────────────────────────────────────────────────────────

    /// @brief メインモジュールを取得
    /// @return メインモジュールの参照
    MainModule& GetMainModule() { return *mainModule_; }

    /// @brief エミッションモジュールを取得
    /// @return エミッションモジュールの参照
    EmissionModule& GetEmissionModule() { return *emissionModule_; }

    /// @brief 形状モジュールを取得
    /// @return 形状モジュールの参照
    ShapeModule& GetShapeModule() { return *shapeModule_; }

    /// @brief 速度モジュールを取得
    /// @return 速度モジュールの参照
    VelocityModule& GetVelocityModule() { return *velocityModule_; }

    /// @brief 色モジュールを取得
    /// @return 色モジュールの参照
    ColorModule& GetColorModule() { return *colorModule_; }

    /// @brief 力場モジュールを取得
    /// @return 力場モジュールの参照
    ForceModule& GetForceModule() { return *forceModule_; }

    /// @brief サイズモジュールを取得
    /// @return サイズモジュールの参照
    SizeModule& GetSizeModule() { return *sizeModule_; }

    /// @brief 回転モジュールを取得
    /// @return 回転モジュールの参照
    RotationModule& GetRotationModule() { return *rotationModule_; }

    /// @brief ノイズモジュールを取得
    /// @return ノイズモジュールの参照
    NoiseModule& GetNoiseModule() { return *noiseModule_; }

    // ──────────────────────────────────────────────────────────
    // 統計情報
    // ──────────────────────────────────────────────────────────

    /// @brief 現在のパーティクル数を取得
    /// @return パーティクル数
    uint32_t GetParticleCount() const { return static_cast<uint32_t>(particles_.size()); }

    /// @brief 最大パーティクル数を取得（MainModuleの設定を反映）
    /// @return 最大パーティクル数
    uint32_t GetMaxParticleCount() const {
        uint32_t maxParticles = mainModule_->GetMainData().maxParticles;
        return (std::min)(maxParticles, kNumMaxInstance);
    }

    struct Statistics {
        uint32_t totalParticlesCreated = 0;
        uint32_t totalParticlesDestroyed = 0;
        uint32_t peakParticleCount = 0;
        float averageLifetime = 0.0f;
        float systemRuntime = 0.0f;
    };

    /// @brief 統計情報を取得
    const Statistics& GetStatistics() const { return statistics_; }

    /// @brief 統計情報をリセット
    void ResetStatistics() {
        statistics_ = Statistics();
        statistics_.systemRuntime = 0.0f;
    }

private:
    // ──────────────────────────────────────────────────────────
    // パーティクルシステムのコア
    // ──────────────────────────────────────────────────────────

    DirectXCommon* dxCommon_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;

    // パーティクルデータ
    std::list<Particle> particles_;
    uint32_t instanceCount_ = 0;

    // エミッター設定
    EulerTransform emitterTransform_;
    BillboardType billboardType_ = BillboardType::ViewFacing;
    BlendMode blendMode_ = BlendMode::kBlendModeAdd;

    // 描画モード
    ParticleRenderMode renderMode_ = ParticleRenderMode::Billboard;

    // テクスチャ（ビルボードモード用）
    TextureManager::LoadedTexture texture_;

    // モデルリソース（モデルモード用）
    ModelResource* modelResource_ = nullptr;

    // 統計情報
    Statistics statistics_;
    float deltaTimeAccumulator_ = 0.0f;
    float lastElapsedTime_ = 0.0f;  // ループ検出用（インスタンスごと）

    // ──────────────────────────────────────────────────────────
    // モジュール
    // ──────────────────────────────────────────────────────────

    std::unique_ptr<MainModule> mainModule_;
    std::unique_ptr<EmissionModule> emissionModule_;
    std::unique_ptr<ShapeModule> shapeModule_;
    std::unique_ptr<VelocityModule> velocityModule_;
    std::unique_ptr<ColorModule> colorModule_;
    std::unique_ptr<ForceModule> forceModule_;
    std::unique_ptr<SizeModule> sizeModule_;
    std::unique_ptr<RotationModule> rotationModule_;
    std::unique_ptr<NoiseModule> noiseModule_;

    std::unique_ptr<ParticlePresetManager> presetManager_ = std::make_unique<ParticlePresetManager>();

    // ──────────────────────────────────────────────────────────
    // Core関連（リファクタリング）
    // ──────────────────────────────────────────────────────────

    std::unique_ptr<ParticleResourceManager> resourceManager_;
    std::unique_ptr<ParticleRenderDataBuilder> renderDataBuilder_;
    std::unique_ptr<ParticleUpdater> particleUpdater_;
    std::unique_ptr<ParticleEmitter> particleEmitter_;

    // ──────────────────────────────────────────────────────────
    // GPU関連リソース（resourceManager_で管理）
    // ──────────────────────────────────────────────────────────

    ParticleForGPU* instancingData_ = nullptr; // resourceManager_から取得したポインタ

#ifdef _DEBUG
    // ──────────────────────────────────────────────────────────
    // デバッグUI（friend宣言）
    // ──────────────────────────────────────────────────────────

    friend class ParticleSystemDebugUI;
#endif
};
}
