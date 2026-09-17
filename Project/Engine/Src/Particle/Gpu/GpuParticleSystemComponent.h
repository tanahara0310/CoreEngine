#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"

#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl.h>

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"
#include "Particle/IParticleSystem.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/NoiseModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/SizeModule.h"
#include "Particle/Modules/VelocityModule.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    class GpuParticleRenderer;
    class GraphicsCore;
    class ParticlePresetManager;

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

static constexpr Cb::Field kGpuParticleParamsFields[] = {
    CB_FIELD(GpuParticleParams, billboardMatrix), CB_FIELD(GpuParticleParams, viewProjection),
    CB_FIELD(GpuParticleParams, emitterPosition), CB_FIELD(GpuParticleParams, deltaTime),
    CB_FIELD(GpuParticleParams, emitCount), CB_FIELD(GpuParticleParams, bufferCapacity),
    CB_FIELD(GpuParticleParams, effectiveCapacity), CB_FIELD(GpuParticleParams, frameSeed),
    CB_FIELD(GpuParticleParams, reset), CB_FIELD(GpuParticleParams, gravityModifier),
    CB_FIELD(GpuParticleParams, pad0), CB_FIELD(GpuParticleParams, pad1), CB_FIELD(GpuParticleParams, startColor),
    CB_FIELD(GpuParticleParams, startSize), CB_FIELD(GpuParticleParams, startSizeRandomness),
    CB_FIELD(GpuParticleParams, startRotation), CB_FIELD(GpuParticleParams, startRotationRandomness),
    CB_FIELD(GpuParticleParams, startLifetime), CB_FIELD(GpuParticleParams, startLifetimeRandomness),
    CB_FIELD(GpuParticleParams, startSpeed), CB_FIELD(GpuParticleParams, startSpeedRandomness),
    CB_FIELD(GpuParticleParams, startColorRandomness), CB_FIELD(GpuParticleParams, shapeEnabled),
    CB_FIELD(GpuParticleParams, velocityEnabled), CB_FIELD(GpuParticleParams, velocityUseRandomDir),
    CB_FIELD(GpuParticleParams, shapeScale), CB_FIELD(GpuParticleParams, shapeType),
    CB_FIELD(GpuParticleParams, shapeRadius), CB_FIELD(GpuParticleParams, shapeInnerRadius),
    CB_FIELD(GpuParticleParams, shapeHeight), CB_FIELD(GpuParticleParams, shapeAngleRad),
    CB_FIELD(GpuParticleParams, shapeEmissionDirection), CB_FIELD(GpuParticleParams, shapeRandomPositionRange),
    CB_FIELD(GpuParticleParams, shapeCirclePlane), CB_FIELD(GpuParticleParams, shapeEmitFromSurface),
    CB_FIELD(GpuParticleParams, pad2), CB_FIELD(GpuParticleParams, pad3),
    CB_FIELD(GpuParticleParams, velocityDirection), CB_FIELD(GpuParticleParams, pad4),
    CB_FIELD(GpuParticleParams, velocityRandomRange), CB_FIELD(GpuParticleParams, pad5),
    CB_FIELD(GpuParticleParams, gravity), CB_FIELD(GpuParticleParams, drag), CB_FIELD(GpuParticleParams, wind),
    CB_FIELD(GpuParticleParams, forceEnabled), CB_FIELD(GpuParticleParams, fieldAcceleration),
    CB_FIELD(GpuParticleParams, useAccelerationField), CB_FIELD(GpuParticleParams, fieldMin),
    CB_FIELD(GpuParticleParams, pad6), CB_FIELD(GpuParticleParams, fieldMax), CB_FIELD(GpuParticleParams, pad7),
    CB_FIELD(GpuParticleParams, endColor), CB_FIELD(GpuParticleParams, colorEnabled),
    CB_FIELD(GpuParticleParams, sizeEnabled), CB_FIELD(GpuParticleParams, sizeUse3D),
    CB_FIELD(GpuParticleParams, sizeCurve), CB_FIELD(GpuParticleParams, endSize3D),
    CB_FIELD(GpuParticleParams, endSize), CB_FIELD(GpuParticleParams, sizeMin),
    CB_FIELD(GpuParticleParams, sizeMax), CB_FIELD(GpuParticleParams, rotationEnabled),
    CB_FIELD(GpuParticleParams, rotationUse2D), CB_FIELD(GpuParticleParams, rotationSpeed3D),
    CB_FIELD(GpuParticleParams, rotation2DSpeed), CB_FIELD(GpuParticleParams, rotationSpeedRandomness3D),
    CB_FIELD(GpuParticleParams, rotation2DSpeedRandomness), CB_FIELD(GpuParticleParams, rotationDirectionMode),
    CB_FIELD(GpuParticleParams, rotationOverLifetime), CB_FIELD(GpuParticleParams, rotStartMul),
    CB_FIELD(GpuParticleParams, rotEndMul), CB_FIELD(GpuParticleParams, noisePositionAmount),
    CB_FIELD(GpuParticleParams, noiseEnabled), CB_FIELD(GpuParticleParams, noiseStrength),
    CB_FIELD(GpuParticleParams, noiseFrequency), CB_FIELD(GpuParticleParams, noiseScrollSpeed),
    CB_FIELD(GpuParticleParams, noiseDamping),
};
CB_VERIFY_LAYOUT(GpuParticleParams, kGpuParticleParamsFields);
CB_BIND_HLSL(GpuParticleParams, kGpuParticleParamsFields, "GpuParticleParams");

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

static constexpr Cb::Field kGpuParticleDataFields[] = {
    CB_FIELD(GpuParticleData, position), CB_FIELD(GpuParticleData, lifeTime), CB_FIELD(GpuParticleData, velocity),
    CB_FIELD(GpuParticleData, currentTime), CB_FIELD(GpuParticleData, rotation), CB_FIELD(GpuParticleData, pad0),
    CB_FIELD(GpuParticleData, rotationSpeed), CB_FIELD(GpuParticleData, pad1),
    CB_FIELD(GpuParticleData, initialScale), CB_FIELD(GpuParticleData, pad2),
    CB_FIELD(GpuParticleData, initialColor),
};
CB_VERIFY_STRIDE(GpuParticleData, kGpuParticleDataFields);

/// @brief 放出・更新・生存管理・描画データの生成をすべて ComputeShader で行うパーティクルのコンポーネント
/// @details パラメータは CPU 版と同じモジュールで編集し、毎フレーム定数バッファへ詰めて GPU に渡す。
///          放出位置は兄弟のトランスフォームのワールド位置（無ければ Awake でトランスフォームを足す）。
///          粒の生存管理はフリーリスト、描画は ExecuteIndirect、生存数は 1 フレーム遅れのリードバックで読む。
class GpuParticleSystemComponent : public IComponent, public IRenderableComponent, public IParticleSystem {
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

    GpuParticleSystemComponent();
    ~GpuParticleSystemComponent() override;

    const char* GetTypeName() const override { return "GpuParticleSystem"; }

    REFLECT_DECLARE(GpuParticleSystemComponent)

    /// @brief 放出位置を取るトランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override;

    /// @brief トランスフォームを確保し、GPU バッファ・UAV / SRV・レンダラー・テクスチャを用意する
    void Awake() override;

    /// @brief 「起動時に再生」なら再生を始める
    void Start() override;

    /// @brief 再生時間と放出数の管理（粒の更新は GPU）
    void Update() override;

    /// @brief モジュールの値をプリセットと同じ形で書き出す
    json OnSerialize() const override;
    void OnDeserialize(const json& j) override;

    // ===== IRenderableComponent =====

    RenderPassType GetRenderPassType() const override { return RenderPassType::GpuParticle; }
    BlendMode GetBlendMode() const override { return blendMode_; }
    void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }

    /// @brief 定数バッファを書き、レンダラーに放出・更新のディスパッチと描画を任せる
    void Render(const DrawViewInfo& view) override;

    // ===== 再生 =====

    /// @brief 最初から再生する（経過時間とバーストを戻す）
    void Play() override;

    /// @brief 放出だけを止める（生きている粒は寿命まで更新する）
    void Stop() override { isPlaying_ = false; }

    bool IsPlaying() const override { return isPlaying_; }

    // ===== プリセット =====

    bool LoadPreset(const std::string& filePath);
    bool SavePreset(const std::string& filePath);
    ParticlePresetManager& GetPresetManager() { return *presetManager_; }

    // ===== 見た目 =====

    /// @brief テクスチャをパスかファイル名で指す（空なら既定のテクスチャ）
    void SetTexture(const std::string& texturePath) override;

    Reflection::AssetRefValue GetTextureAsset() const { return textureAsset_.GetValue(); }
    void SetTextureAsset(const Reflection::AssetRefValue& value);

    D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle() const { return texture_.gpuHandle; }

    /// @brief 板ポリの向き
    void SetBillboardType(BillboardType type) override { billboardType_ = type; }
    BillboardType GetBillboardType() const override { return billboardType_; }

    /// @brief 放出位置（兄弟のトランスフォームのワールド位置）
    Vector3 GetEmitterPosition() const override;

    // ===== モジュール =====

    MainModule& GetMainModule() override { return *mainModule_; }
    EmissionModule& GetEmissionModule() override { return *emissionModule_; }
    ShapeModule& GetShapeModule() override { return *shapeModule_; }
    VelocityModule& GetVelocityModule() override { return *velocityModule_; }
    ColorModule& GetColorModule() override { return *colorModule_; }
    ForceModule& GetForceModule() override { return *forceModule_; }
    SizeModule& GetSizeModule() override { return *sizeModule_; }
    RotationModule& GetRotationModule() override { return *rotationModule_; }
    NoiseModule& GetNoiseModule() override { return *noiseModule_; }

    // ===== レンダラーが読む値 =====

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

    // バリアを張る側はこちら（ステートは GpuResource が持つ）
    GpuResource& Instancing() { return instancingResource_; }
    GpuResource& Counter() { return counterResource_; }
    GpuResource& Args() { return argsResource_; }
    GpuResource& FreeList() { return freeListResource_; }
    ID3D12Resource* GetUploadInitResource() const { return uploadInitResource_.Get(); }
    ID3D12Resource* GetReadbackResource() const { return readbackResource_.Get(); }

    D3D12_GPU_DESCRIPTOR_HANDLE GetParticleUavHandleGPU() const { return particleUavGPU_.gpuHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetCounterUavHandleGPU() const { return counterUavGPU_.gpuHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetFreeListUavHandleGPU() const { return freeListUavGPU_.gpuHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingUavHandleGPU() const { return instancingUavGPU_.gpuHandle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetInstancingSrvHandleGPU() const { return instancingSrvGPU_.gpuHandle; }

    ID3D12Resource* GetFreeListResource() const { return freeListResource_.Get(); }

    // ===== 統計（リードバック。1フレーム遅延） =====

    /// @brief 生存パーティクル数を取得（1フレーム遅延）
    uint32_t GetAliveCount() const { return readbackData_ ? readbackData_[kCounterAliveIndex] : 0; }

    /// @brief フリーリストの残数を取得（1フレーム遅延）
    uint32_t GetFreeCount() const { return readbackData_ ? readbackData_[kCounterFreeTopIndex] : 0; }

    /// @brief 実効的な最大生存数を取得（MainModule.maxParticles を反映）
    uint32_t GetEffectiveCapacity() const;

private:
    /// @brief GPU バッファ・UAV / SRV・定数バッファを作る
    /// @return 作れたら true
    bool CreateGpuResources(GraphicsCore& graphics);

    /// @brief UAV付きDEFAULTヒープバッファを作成
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBuffer(ID3D12Device* device, size_t sizeInBytes);

    /// @brief 指しているテクスチャ（無ければ既定）を読み込む
    void LoadTexture();

    /// @brief カメラの向きに合わせた板ポリの回転を作る
    Matrix4x4 MakeBillboardMatrix(const Matrix4x4& viewMatrix) const;

    // ──────────────────────────────────────────────────────────
    // GPUリソース
    // ──────────────────────────────────────────────────────────

    Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;   // GpuParticleData × kMaxParticles（UAV固定）
    GpuResource counterResource_;    // uint × 4（freeTop/alive/draw、UAV⇔COPY間遷移）
    GpuResource freeListResource_;   // uint × kMaxParticles（死亡スロットスタック）
    GpuResource instancingResource_; // ParticleForGPU互換 × kMaxParticles（コンパクション済み）
    GpuResource argsResource_;       // D3D12_DRAW_ARGUMENTS（ExecuteIndirect用）
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadInitResource_; // 初期化データ（引数/カウンタ/フリーリスト）のコピー元
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackResource_;   // カウンタのリードバック（統計用）
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;     // 定数バッファ（UPLOAD・永続Map）
    GpuParticleParams* paramsData_ = nullptr;
    uint32_t* readbackData_ = nullptr;                          // readbackResource_ の永続Map

    DescriptorHandle particleUavGPU_ = {};
    DescriptorHandle counterUavGPU_ = {};
    DescriptorHandle freeListUavGPU_ = {};
    DescriptorHandle instancingUavGPU_ = {};
    DescriptorHandle instancingSrvGPU_ = {};

    GpuParticleRenderer* renderer_ = nullptr;
    bool awoken_ = false;

    AssetRef<TextureAsset> textureAsset_;
    TextureManager::LoadedTexture texture_{};

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

    // プリセット管理（CPU版と共通のJSONフォーマット）
    std::unique_ptr<ParticlePresetManager> presetManager_;

    BillboardType billboardType_ = BillboardType::ViewFacing;
    BlendMode blendMode_ = BlendMode::kBlendModeAdd;

    bool isPlaying_ = false;
    bool resetPending_ = true;         // 初回フレームでバッファをGPU側初期化する
    float elapsedTime_ = 0.0f;         // 再生経過時間（duration/looping 判定用）
    bool burstDone_ = false;           // 今ループでバースト済みか
    float emitAccumulator_ = 0.0f;     // 放出レートの端数積算
    uint32_t emitCountThisFrame_ = 0;
    uint32_t frameSeed_ = 0;
};
}
