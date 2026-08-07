#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <memory>

// Math
#include "Math/MathCore.h"

// GameObject基底クラス
#include "GameObject/GameObject.h"
#include "GameObject/Component/EulerTransformComponent.h"

// テクスチャ
#include "Graphics/Texture/TextureManager.h"

// CPU/GPU共通インターフェース
#include "Particle/IParticleSystem.h"

// プリセット管理（CPU版と共通）
#include "Particle/ParticlePresetManager.h"

// モジュール（CPU版とパラメータ定義・ImGuiを共有。CPU用更新メソッドは使わない）
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/VelocityModule.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/SizeModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/NoiseModule.h"

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class ResourceFactory;
    class Camera;
}

namespace CoreEngine
{

/// @brief GPUパーティクル用定数バッファ（GpuParticle.hlsli の GpuParticleParams と一致・576バイト）
struct GpuParticleParams {
    Matrix4x4 billboardMatrix;   // ビルボード回転（平行移動なし）
    Matrix4x4 viewProjection;

    // ===== フレーム情報 =====
    Vector3 emitterPosition;
    float deltaTime;
    uint32_t emitCount;
    uint32_t bufferCapacity;     // パーティクルバッファの物理容量（フリーリスト容量）
    uint32_t effectiveCapacity;  // 生存数の上限（MainModule.maxParticles）
    uint32_t frameSeed;
    uint32_t reset;
    float gravityModifier;
    uint32_t pad0;
    uint32_t pad1;

    // ===== Main =====
    Vector4 startColor;
    Vector3 startSize;
    float startSizeRandomness;
    Vector3 startRotation;       // ラジアン
    float startRotationRandomness;
    float startLifetime;
    float startLifetimeRandomness;
    float startSpeed;
    float startSpeedRandomness;
    float startColorRandomness;
    uint32_t shapeEnabled;
    uint32_t velocityEnabled;
    uint32_t velocityUseRandomDir;

    // ===== Shape =====
    Vector3 shapeScale;
    uint32_t shapeType;
    float shapeRadius;
    float shapeInnerRadius;
    float shapeHeight;
    float shapeAngleRad;         // ラジアン
    Vector3 shapeEmissionDirection;
    float shapeRandomPositionRange;
    uint32_t shapeCirclePlane;
    uint32_t shapeEmitFromSurface;
    uint32_t pad2;
    uint32_t pad3;

    // ===== Velocity =====
    Vector3 velocityDirection;
    uint32_t pad4;
    Vector3 velocityRandomRange;
    uint32_t pad5;

    // ===== Force =====
    Vector3 gravity;
    float drag;
    Vector3 wind;
    uint32_t forceEnabled;
    Vector3 fieldAcceleration;
    uint32_t useAccelerationField;
    Vector3 fieldMin;
    uint32_t pad6;
    Vector3 fieldMax;
    uint32_t pad7;

    // ===== Color =====
    Vector4 endColor;
    uint32_t colorEnabled;
    uint32_t sizeEnabled;
    uint32_t sizeUse3D;
    uint32_t sizeCurve;

    // ===== Size =====
    Vector3 endSize3D;
    float endSize;
    float sizeMin;
    float sizeMax;
    uint32_t rotationEnabled;
    uint32_t rotationUse2D;

    // ===== Rotation =====
    Vector3 rotationSpeed3D;
    float rotation2DSpeed;
    Vector3 rotationSpeedRandomness3D;
    float rotation2DSpeedRandomness;
    uint32_t rotationDirectionMode;
    uint32_t rotationOverLifetime;
    float rotStartMul;
    float rotEndMul;

    // ===== Noise =====
    Vector3 noisePositionAmount;
    uint32_t noiseEnabled;
    float noiseStrength;
    float noiseFrequency;
    float noiseScrollSpeed;
    uint32_t noiseDamping;
};
static_assert(sizeof(GpuParticleParams) == 576,
    "GpuParticleParams は HLSL 側 GpuParticleParams の 576 バイトレイアウトと一致させること");

/// @brief GPUパーティクル1個分の状態（GpuParticle.hlsli の GpuParticle と一致・96バイト、サイズ確認用）
struct GpuParticleData {
    Vector3 position;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    Vector3 rotation;
    float pad0;
    Vector3 rotationSpeed;
    float pad1;
    Vector3 initialScale;
    float pad2;
    Vector4 initialColor;
};
static_assert(sizeof(GpuParticleData) == 96,
    "GpuParticleData は HLSL 側 GpuParticle の 96 バイトレイアウトと一致させること");

/// @brief GPUパーティクルシステム（Phase 3: フリーリスト + ExecuteIndirect + リードバック）
/// 放出・更新・生存管理・描画データ生成をすべてComputeShaderで行う。
/// パラメータはCPU版と同じモジュール群で編集し、毎フレームCBへ詰めてGPUに渡す。
/// 設計は Docs/Engine/Particle/GpuParticleSystem.md 参照。
class GpuParticleSystem : public GameObject, public IParticleSystem {
public:
    // パーティクルバッファの物理容量（生存数の上限は MainModule.maxParticles でさらに制限）
    static constexpr uint32_t kMaxParticles = 65536;

    // カウンタバッファのレイアウト（uint×4）
    static constexpr uint32_t kCounterCount = 4;
    static constexpr uint32_t kCounterFreeTopIndex = 0;
    static constexpr uint32_t kCounterAliveIndex = 1;
    static constexpr uint32_t kCounterDrawIndex = 2;

    // 初期化用アップロードバッファ内のオフセット
    // 注意: GPU初期化はCS（定数バッファのresetフラグ）ではなくコピーで行う。
    // CBは1面のみでCPUが毎フレーム上書きするため、フレームパイプライン中に
    // GPUが読む前に一度きりのフラグが潰される競合がある（実測で発生）。
    static constexpr uint64_t kInitArgsOffset = 0;      // D3D12_DRAW_ARGUMENTS {6,0,0,0}
    static constexpr uint64_t kInitCountersOffset = 16; // {kMaxParticles, 0, 0, 0}
    static constexpr uint64_t kInitFreeListOffset = 32; // {0, 1, ..., kMaxParticles-1}

    /// @brief コンストラクタ
    /// @note エミッタ位置の実体は `EulerTransformComponent` が持つ。
    ///       `emitterPosition_` はその translate への参照なので既存コードは変わらない。
    GpuParticleSystem()
        : emitterTransformComponent_(AddComponent<EulerTransformComponent>())
        , emitterPosition_(emitterTransformComponent_->Translate()) {}
    ~GpuParticleSystem() override = default;

    /// @brief 初期化（GPUバッファ・UAV/SRV・モジュールの生成）
    void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory, const std::string& name = "") override;

    /// @brief 更新処理（再生時間・放出数の管理のみ。粒子更新はGPU）
    void Update() override;

    /// @brief ワールド空間での位置を取得（エミッタ位置）
    Vector3 GetWorldPosition() const override { return emitterPosition_; }

    /// @brief 描画前処理（定数バッファへの書き込み。ディスパッチはレンダラーが行う）
    void Draw(const Camera* camera) override;

    // ──────────────────────────────────────────────────────────
    // GameObjectインターフェース実装
    // ──────────────────────────────────────────────────────────

    RenderPassType GetRenderPassType() const override { return RenderPassType::GpuParticle; }

    void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }
    BlendMode GetBlendMode() const override { return blendMode_; }

#ifdef USE_IMGUI
    const char* GetObjectName() const override { return "GpuParticleSystem"; }

    /// @brief インスペクタータブ定義を返す（CPU版 ParticleSystem と同じタブ構成）
    int GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;

    /// @brief 指定タブのコンテンツを描画する
    bool DrawInspectorTabContent(int tabIndex) override;
#endif

    // ──────────────────────────────────────────────────────────
    // 制御
    // ──────────────────────────────────────────────────────────

    /// @brief 再生開始
    void Play() override;

    /// @brief 再生停止（放出のみ止まる。生存中の粒子は寿命まで更新される）
    void Stop() override { isPlaying_ = false; }

    /// @brief 再生中かどうか
    bool IsPlaying() const override { return isPlaying_; }

    /// @brief テクスチャを設定
    void SetTexture(const std::string& texturePath) override;

    /// @brief テクスチャハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle() const { return texture_.gpuHandle; }

    // ──────────────────────────────────────────────────────────
    // 設定アクセッサ
    // ──────────────────────────────────────────────────────────

    void SetEmitterPosition(const Vector3& position) override { emitterPosition_ = position; }
    Vector3 GetEmitterPosition() const override { return emitterPosition_; }

    /// @brief ビルボードタイプを設定（None/ViewFacing/YAxisOnly/ScreenAligned）
    void SetBillboardType(BillboardType type) override { billboardType_ = type; }
    BillboardType GetBillboardType() const override { return billboardType_; }

    /// @brief 放出レート（個/秒）を設定（EmissionModule.rateOverTime への転送）
    void SetEmissionRate(float rate) {
        emissionModule_->GetEmissionData().rateOverTime = static_cast<uint32_t>(rate);
    }

    void SetStartLifetime(float lifetime) { mainModule_->GetMainData().startLifetime = lifetime; }
    void SetStartSpeed(float speed) { mainModule_->GetMainData().startSpeed = speed; }
    void SetStartScale(float scale) { mainModule_->GetMainData().startSize = { scale, scale, scale }; }
    void SetStartColor(const Vector4& color) { mainModule_->GetMainData().startColor = color; }

    // ──────────────────────────────────────────────────────────
    // モジュールアクセッサ（CPU版 ParticleSystem と同形）
    // ──────────────────────────────────────────────────────────

    MainModule& GetMainModule() override { return *mainModule_; }
    EmissionModule& GetEmissionModule() override { return *emissionModule_; }
    ShapeModule& GetShapeModule() override { return *shapeModule_; }
    VelocityModule& GetVelocityModule() override { return *velocityModule_; }
    ColorModule& GetColorModule() override { return *colorModule_; }
    ForceModule& GetForceModule() override { return *forceModule_; }
    SizeModule& GetSizeModule() override { return *sizeModule_; }
    RotationModule& GetRotationModule() override { return *rotationModule_; }
    NoiseModule& GetNoiseModule() override { return *noiseModule_; }

    // ──────────────────────────────────────────────────────────
    // レンダラーがアクセスするためのゲッター
    // ──────────────────────────────────────────────────────────

    /// @brief 今フレームの放出数
    uint32_t GetEmitCount() const { return emitCountThisFrame_; }

    /// @brief 初期化ディスパッチが必要か（初回フレーム）
    bool IsResetPending() const { return resetPending_; }

    /// @brief 初期化ディスパッチ完了をレンダラーが通知する
    void ClearResetPending() { resetPending_ = false; }

    /// @brief 定数バッファのGPUアドレス
    D3D12_GPU_VIRTUAL_ADDRESS GetParamsGPUAddress() const { return paramsResource_->GetGPUVirtualAddress(); }

    ID3D12Resource* GetParticleResource() const { return particleResource_.Get(); }
    ID3D12Resource* GetInstancingResource() const { return instancingResource_.Get(); }
    ID3D12Resource* GetCounterResource() const { return counterResource_.Get(); }
    ID3D12Resource* GetArgsResource() const { return argsResource_.Get(); }
    ID3D12Resource* GetUploadInitResource() const { return uploadInitResource_.Get(); }
    ID3D12Resource* GetReadbackResource() const { return readbackResource_.Get(); }

    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleUavHandleGPU() const { return particleUavGPU_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetCounterUavHandleGPU() const { return counterUavGPU_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetFreeListUavHandleGPU() const { return freeListUavGPU_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingUavHandleGPU() const { return instancingUavGPU_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingSrvHandleGPU() const { return instancingSrvGPU_; }

    /// @brief インスタンシングバッファの現在状態（レンダラーが遷移管理に使う）
    D3D12_RESOURCE_STATES GetInstancingState() const { return instancingState_; }
    void SetInstancingState(D3D12_RESOURCE_STATES state) { instancingState_ = state; }

    /// @brief カウンタバッファの現在状態
    D3D12_RESOURCE_STATES GetCounterState() const { return counterState_; }
    void SetCounterState(D3D12_RESOURCE_STATES state) { counterState_ = state; }

    /// @brief 間接引数バッファの現在状態
    D3D12_RESOURCE_STATES GetArgsState() const { return argsState_; }
    void SetArgsState(D3D12_RESOURCE_STATES state) { argsState_ = state; }

    /// @brief フリーリストバッファの現在状態
    D3D12_RESOURCE_STATES GetFreeListState() const { return freeListState_; }
    void SetFreeListState(D3D12_RESOURCE_STATES state) { freeListState_ = state; }

    ID3D12Resource* GetFreeListResource() const { return freeListResource_.Get(); }

    // ──────────────────────────────────────────────────────────
    // 統計（リードバック。1フレーム遅延）
    // ──────────────────────────────────────────────────────────

    /// @brief 生存パーティクル数を取得（1フレーム遅延）
    uint32_t GetAliveCount() const { return readbackData_ ? readbackData_[kCounterAliveIndex] : 0; }

    /// @brief フリーリストの残数を取得（1フレーム遅延）
    uint32_t GetFreeCount() const { return readbackData_ ? readbackData_[kCounterFreeTopIndex] : 0; }

    /// @brief 実効的な最大生存数を取得（MainModule.maxParticles を反映）
    uint32_t GetEffectiveCapacity() const;

private:
    /// @brief UAV付きDEFAULTヒープバッファを作成
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBuffer(size_t sizeInBytes);

    DirectXCommon* dxCommon_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;

    // ──────────────────────────────────────────────────────────
    // GPUリソース
    // ──────────────────────────────────────────────────────────

    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;   // GpuParticleData × kMaxParticles（UAV固定）
    Microsoft::WRL::ComPtr<ID3D12Resource> counterResource_;    // uint × 4（freeTop/alive/draw、UAV⇔COPY間遷移）
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;   // uint × kMaxParticles（死亡スロットスタック、UAV固定）
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_; // ParticleForGPU互換 × kMaxParticles（コンパクション済み）
    Microsoft::WRL::ComPtr<ID3D12Resource> argsResource_;       // D3D12_DRAW_ARGUMENTS（ExecuteIndirect用）
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadInitResource_; // 初期化データ（引数/カウンタ/フリーリスト）のコピー元
    D3D12_RESOURCE_STATES freeListState_ = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackResource_;   // カウンタのリードバック（統計用）
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;     // 定数バッファ（UPLOAD・永続Map）
    GpuParticleParams* paramsData_ = nullptr;
    uint32_t* readbackData_ = nullptr;                          // readbackResource_ の永続Map

    D3D12_GPU_DESCRIPTOR_HANDLE particleUavGPU_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE counterUavGPU_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE freeListUavGPU_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE instancingUavGPU_ = {};
    D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvGPU_ = {};

    D3D12_RESOURCE_STATES instancingState_ = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES counterState_ = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES argsState_ = D3D12_RESOURCE_STATE_COMMON;

    // テクスチャ
    TextureManager::LoadedTexture texture_;

    // ──────────────────────────────────────────────────────────
    // モジュール（パラメータコンテナ + ImGui として使用）
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

    // ──────────────────────────────────────────────────────────
    // エミッター設定・再生状態
    // ──────────────────────────────────────────────────────────

    /// トランスフォームの実体を持つコンポーネント（コンストラクタでアタッチ済み）
    /// @note GPU 版はエミッタ位置しか使わないが、コンポーネント化することで
    ///       ギズモ・インスペクタが具象型を知らずにエミッタを動かせるようになる。
    EulerTransformComponent* emitterTransformComponent_ = nullptr;

    /// エミッタ位置（`emitterTransformComponent_` が持つ translate への参照）
    Vector3& emitterPosition_;

    BillboardType billboardType_ = BillboardType::ViewFacing;
    BlendMode blendMode_ = BlendMode::kBlendModeAdd;

    // プリセット管理（CPU版と共通のJSONフォーマット）
    std::unique_ptr<ParticlePresetManager> presetManager_ = std::make_unique<ParticlePresetManager>();

#ifdef USE_IMGUI
    // 開発UI（CPU版と共通実装）からプリセットマネージャーへアクセスするため
    friend class ParticleSystemDebugUI;
#endif

    bool isPlaying_ = false;
    bool resetPending_ = true;         // 初回フレームでバッファをGPU側初期化する
    float elapsedTime_ = 0.0f;         // 再生経過時間（duration/looping 判定用）
    bool burstDone_ = false;           // 今ループでバースト済みか
    float emitAccumulator_ = 0.0f;     // 放出レートの端数積算
    uint32_t emitCountThisFrame_ = 0;
    uint32_t frameSeed_ = 0;
};
}
