#include "pch.h"
#include "Particle/Gpu/GpuParticleSystemComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/Particle/GpuParticleRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Particle/Core/ParticleResourceManager.h" // ParticleForGPU（インスタンスデータレイアウト共有）
#include "Particle/ParticlePresetManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    /// @brief ブレンドの名前（`BlendMode` の並び）
    constexpr const char* kBlendModeNames[] = { "なし", "アルファ", "加算", "減算", "乗算", "スクリーン" };
    static_assert(std::size(kBlendModeNames) == CoreEngine::kBlendModeCount);

    /// @brief ビルボードの名前（`BillboardType` の並び）
    constexpr const char* kBillboardNames[] = { "なし（粒の回転だけ）", "カメラの正面", "Y 軸だけ回す", "画面と平行" };

    /// @brief テクスチャを指していないときに貼るテクスチャ
    constexpr const char* kDefaultTexture = "circle.png";

    constexpr float kDegToRad = CoreEngine::MathCore::Constants::kDegToRad;

    /// @brief テクスチャを指していないときにテクスチャの欄へ出す文字
    std::string DescribeDefaultTexture(const void*)
    {
        return std::string(kDefaultTexture) + "（既定）";
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::GpuParticleSystemComponent, "GPU パーティクル")
    REFLECT_PARTIAL()
    REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
        p.assetType = ::CoreEngine::AssetType::Texture, p.emptyText = &DescribeDefaultTexture)
    REFLECT_ENUM_ACCESSOR("blendMode", "ブレンド", GetBlendMode, SetBlendMode, kBlendModeNames,
        p.tooltip = "背景との合成のしかた。炎や光は「加算」、煙は「アルファ」が向く")
    REFLECT_ENUM_ACCESSOR("billboard", "ビルボード", GetBillboardType, SetBillboardType, kBillboardNames,
        p.tooltip = "板ポリの向き。草や炎の柱のように立てたいものは「Y 軸だけ回す」")
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::GpuParticleSystemComponent)
COMPONENT_REGISTER(CoreEngine::GpuParticleSystemComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    GpuParticleSystemComponent::GpuParticleSystemComponent()
        : mainModule_(std::make_unique<MainModule>())
        , emissionModule_(std::make_unique<EmissionModule>())
        , shapeModule_(std::make_unique<ShapeModule>())
        , velocityModule_(std::make_unique<VelocityModule>())
        , colorModule_(std::make_unique<ColorModule>())
        , forceModule_(std::make_unique<ForceModule>())
        , sizeModule_(std::make_unique<SizeModule>())
        , rotationModule_(std::make_unique<RotationModule>())
        , noiseModule_(std::make_unique<NoiseModule>())
        , presetManager_(std::make_unique<ParticlePresetManager>())
    {
        // GPU で動くことをモジュールへ知らせる（インスペクタで CPU 専用の項目を隠す）
        ParticleModule* const modules[] = {
            mainModule_.get(), emissionModule_.get(), shapeModule_.get(),
            velocityModule_.get(), colorModule_.get(), forceModule_.get(),
            sizeModule_.get(), rotationModule_.get(), noiseModule_.get(),
        };
        for (ParticleModule* const module : modules) {
            module->SetGpuBackend(true);
        }

        // インスペクタの最大数の欄をバッファの容量までにする
        mainModule_->SetCapacityLimit(kMaxParticles);
    }

    GpuParticleSystemComponent::~GpuParticleSystemComponent()
    {
        if (paramsResource_ && paramsData_) {
            paramsResource_->Unmap(0, nullptr);
        }
        if (readbackResource_ && readbackData_) {
            readbackResource_->Unmap(0, nullptr);
        }
    }

    void GpuParticleSystemComponent::Awake()
    {
        GameObject* const owner = GetOwner();
        if (!owner) { return; }

        // 放出位置は兄弟のトランスフォームから取る。無ければ足す
        if (!owner->GetComponent<ITransformSource>()) {
            owner->AddComponent<TransformComponent>();
        }

        EngineSystem* const engine = owner->GetEngineSystem();
        GraphicsCore* const graphics = engine ? engine->GetService<GraphicsCore>() : nullptr;
        RenderManager* const renderManager = engine ? engine->GetService<RenderManager>() : nullptr;
        if (!graphics || !renderManager) { return; }

        if (!CreateGpuResources(*graphics)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "GpuParticleSystemComponent: \"{}\" の GPU バッファを作れませんでした", owner->GetName());
            return;
        }
        renderer_ = dynamic_cast<GpuParticleRenderer*>(renderManager->GetRenderer(RenderPassType::GpuParticle));

        awoken_ = true;
        LoadTexture();
    }

    bool GpuParticleSystemComponent::CreateGpuResources(GraphicsCore& graphics)
    {
        ID3D12Device* const device = graphics.GetDevice();
        DescriptorAllocator* const descriptorAllocator = graphics.GetDescriptorAllocator();
        if (!device || !descriptorAllocator) { return false; }

        // UAV バッファ（DEFAULT ヒープ）
        particleResource_ = CreateUavBuffer(device, sizeof(GpuParticleData) * kMaxParticles);
        auto counter = CreateUavBuffer(device, sizeof(uint32_t) * kCounterCount);
        auto freeList = CreateUavBuffer(device, sizeof(uint32_t) * kMaxParticles);
        auto instancing = CreateUavBuffer(device, sizeof(ParticleForGPU) * kMaxParticles);
        if (!particleResource_ || !counter || !freeList || !instancing) { return false; }
        counterResource_.Reset(std::move(counter), D3D12_RESOURCE_STATE_COMMON);
        freeListResource_.Reset(std::move(freeList), D3D12_RESOURCE_STATE_COMMON);
        instancingResource_.Reset(std::move(instancing), D3D12_RESOURCE_STATE_COMMON);

        // 間接引数バッファ（ExecuteIndirect 用。コピー先と間接引数だけに使う）
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = 256; // D3D12_DRAW_ARGUMENTS(16B) を256バイトアライン
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Microsoft::WRL::ComPtr<ID3D12Resource> args;
            if (FAILED(device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&args)))) {
                return false;
            }
            argsResource_.Reset(std::move(args), D3D12_RESOURCE_STATE_COMMON);
        }

        // 初期化用アップロードバッファ（CopyBufferRegion のコピー元。オフセット定数はヘッダ参照）
        //   [0..15] 間接引数 / [16..31] カウンタ初期値 / [32..] フリーリスト初期値
        // パーティクルバッファ自体はコミットリソースの OS ゼロ初期化（lifeTime=0=死亡）に依存する。
        uploadInitResource_ = ResourceFactory::CreateBufferResource(
            device, kInitFreeListOffset + sizeof(uint32_t) * kMaxParticles);
        {
            uint32_t* mapped = nullptr;
            uploadInitResource_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
            mapped[0] = 6u; // ビルボード四角形の頂点数
            mapped[1] = 0u;
            mapped[2] = 0u;
            mapped[3] = 0u;
            mapped[4] = kMaxParticles; // freeTop
            mapped[5] = 0u;            // alive
            mapped[6] = 0u;            // draw
            mapped[7] = 0u;
            for (uint32_t i = 0; i < kMaxParticles; ++i) {
                mapped[8 + i] = i;
            }
            uploadInitResource_->Unmap(0, nullptr);
        }

        // カウンタのリードバックバッファ（統計用・永続Map）
        readbackResource_ = ResourceFactory::CreateBufferResource(
            device, sizeof(uint32_t) * kCounterCount, D3D12_HEAP_TYPE_READBACK);
        readbackResource_->Map(0, nullptr, reinterpret_cast<void**>(&readbackData_));

        // UAV / SRV
        const auto makeUavDesc = [](UINT elementCount, UINT stride) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = elementCount;
            uavDesc.Buffer.StructureByteStride = stride;
            return uavDesc;
        };
        particleUavGPU_ = descriptorAllocator->CreateUAV(particleResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(GpuParticleData)), "GpuParticleUAV");
        counterUavGPU_ = descriptorAllocator->CreateUAV(counterResource_.Get(),
            makeUavDesc(kCounterCount, sizeof(uint32_t)), "GpuParticleCounterUAV");
        freeListUavGPU_ = descriptorAllocator->CreateUAV(freeListResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(uint32_t)), "GpuParticleFreeListUAV");
        instancingUavGPU_ = descriptorAllocator->CreateUAV(instancingResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(ParticleForGPU)), "GpuParticleInstancingUAV");
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = kMaxParticles;
            srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
            instancingSrvGPU_ = descriptorAllocator->CreateSRV(instancingResource_.Get(), srvDesc, "GpuParticleInstancingSRV");
        }

        // 定数バッファ（UPLOAD・永続Map）
        paramsResource_ = ResourceFactory::CreateBufferResource(device, sizeof(GpuParticleParams));
        paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
        *paramsData_ = GpuParticleParams{};
        return true;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> GpuParticleSystemComponent::CreateUavBuffer(ID3D12Device* device, size_t sizeInBytes)
    {
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        const size_t alignedSize = (sizeInBytes + 255) & ~static_cast<size_t>(0xFF);

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = alignedSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if (FAILED(device->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)))) {
            return nullptr;
        }
        return buffer;
    }

    void GpuParticleSystemComponent::Start()
    {
        if (mainModule_->GetMainData().playOnAwake) {
            Play();
        }
    }

    void GpuParticleSystemComponent::Play()
    {
        isPlaying_ = true;
        elapsedTime_ = 0.0f;
        burstDone_ = false;
        emitAccumulator_ = 0.0f;
    }

    uint32_t GpuParticleSystemComponent::GetEffectiveCapacity() const
    {
        return std::clamp(mainModule_->GetMainData().maxParticles, 1u, kMaxParticles);
    }

    void GpuParticleSystemComponent::Update()
    {
        const float deltaTime = Time::DeltaTime();

        emitCountThisFrame_ = 0;
        ++frameSeed_;

        if (!isPlaying_ || !mainModule_->IsEnabled()) {
            return;
        }

        const auto& mainData = mainModule_->GetMainData();
        const auto& emissionData = emissionModule_->GetEmissionData();

        elapsedTime_ += deltaTime;

        // 長さとループの扱いは CPU 版と同じ
        if (elapsedTime_ >= mainData.duration) {
            if (mainData.looping) {
                elapsedTime_ = std::fmod(elapsedTime_, mainData.duration);
                burstDone_ = false; // ループごとにバーストを出し直す
            } else {
                // 一度きりなら放出だけ止め、生きている粒は GPU が更新し続ける
                return;
            }
        }

        if (emissionModule_->IsEnabled()) {
            emitAccumulator_ += static_cast<float>(emissionData.rateOverTime) * deltaTime;
            emitCountThisFrame_ = static_cast<uint32_t>(emitAccumulator_);
            emitAccumulator_ -= static_cast<float>(emitCountThisFrame_);

            // バーストはループ内で 1 回、バーストの時刻を過ぎたときに出す
            if (emissionData.burstCount > 0 && !burstDone_ && elapsedTime_ >= emissionData.burstTime) {
                emitCountThisFrame_ += emissionData.burstCount;
                burstDone_ = true;
            }
        }

        // 容量を超えた分は放出の CS が捨てるので、1 フレームの放出数は容量までにする
        emitCountThisFrame_ = (std::min)(emitCountThisFrame_, GetEffectiveCapacity());
    }

    Matrix4x4 GpuParticleSystemComponent::MakeBillboardMatrix(const Matrix4x4& viewMatrix) const
    {
        // CPU 版 ParticleRenderDataBuilder::CreateBillboardMatrix と同じ式
        Matrix4x4 billboardMatrix = Matrix::Identity();
        switch (billboardType_) {
        case BillboardType::ViewFacing:
        {
            billboardMatrix = Matrix::Inverse(viewMatrix);
            billboardMatrix.m[3][0] = 0.0f;
            billboardMatrix.m[3][1] = 0.0f;
            billboardMatrix.m[3][2] = 0.0f;
            break;
        }
        case BillboardType::YAxisOnly:
        {
            const Matrix4x4 invView = Matrix::Inverse(viewMatrix);
            const Vector3 cameraPos = { invView.m[3][0], invView.m[3][1], invView.m[3][2] };
            const float horizontalLength = std::sqrt(cameraPos.x * cameraPos.x + cameraPos.z * cameraPos.z);

            Vector3 forward;
            Vector3 right;
            if (horizontalLength < 0.0001f) {
                forward = { 0.0f, 0.0f, 1.0f };
                right = { 1.0f, 0.0f, 0.0f };
            } else {
                forward = { cameraPos.x / horizontalLength, 0.0f, cameraPos.z / horizontalLength };
                right = { -forward.z, 0.0f, forward.x };
            }

            billboardMatrix.m[0][0] = right.x;   billboardMatrix.m[0][1] = right.y;   billboardMatrix.m[0][2] = right.z;
            billboardMatrix.m[1][0] = 0.0f;      billboardMatrix.m[1][1] = 1.0f;      billboardMatrix.m[1][2] = 0.0f;
            billboardMatrix.m[2][0] = forward.x; billboardMatrix.m[2][1] = forward.y; billboardMatrix.m[2][2] = forward.z;
            break;
        }
        case BillboardType::ScreenAligned:
        {
            const Matrix4x4 invView = Matrix::Inverse(viewMatrix);
            for (int row = 0; row < 3; ++row) {
                billboardMatrix.m[row][0] = invView.m[row][0];
                billboardMatrix.m[row][1] = invView.m[row][1];
                billboardMatrix.m[row][2] = invView.m[row][2];
            }
            break;
        }
        case BillboardType::None:
        default:
            break; // 単位行列（回転は粒自身の回転だけ）
        }
        return billboardMatrix;
    }

    void GpuParticleSystemComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        const Camera* const camera = view.GetCamera();
        if (!owner || !owner->IsActive() || !camera || !paramsData_) {
            return;
        }

        const Matrix4x4 viewMatrix = camera->GetViewMatrix();
        const Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

        const auto& mainData = mainModule_->GetMainData();
        const auto& shapeData = shapeModule_->GetShapeData();
        const auto& velocityData = velocityModule_->GetVelocityData();
        const auto& colorData = colorModule_->GetColorData();
        const auto& forceData = forceModule_->GetForceData();
        const auto& sizeData = sizeModule_->GetSizeData();
        const auto& rotationData = rotationModule_->GetRotationData();
        const auto& noiseData = noiseModule_->GetNoiseData();

        GpuParticleParams& p = *paramsData_;

        p.billboardMatrix = MakeBillboardMatrix(viewMatrix);
        p.viewProjection = viewMatrix * projectionMatrix;

        // ===== フレーム情報 =====
        p.emitterPosition = GetEmitterPosition();
        p.deltaTime = Time::DeltaTime();
        p.emitCount = emitCountThisFrame_;
        p.bufferCapacity = kMaxParticles;
        p.effectiveCapacity = GetEffectiveCapacity();
        p.frameSeed = frameSeed_;
        // GPU の初期化はレンダラーのコピーで行うので、CS の reset は常に 0
        // （CB1面の毎フレーム上書きと一度きりフラグはフレームパイプラインで競合する）
        p.reset = 0u;
        p.gravityModifier = mainData.gravityModifier;

        // ===== Main =====
        p.startColor = mainData.startColor;
        p.startSize = mainData.startSize;
        p.startSizeRandomness = mainData.startSizeRandomness;
        p.startRotation = {
            mainData.startRotation.x * kDegToRad,
            mainData.startRotation.y * kDegToRad,
            mainData.startRotation.z * kDegToRad,
        };
        p.startRotationRandomness = mainData.startRotationRandomness;
        p.startLifetime = mainData.startLifetime;
        p.startLifetimeRandomness = mainData.startLifetimeRandomness;
        p.startSpeed = mainData.startSpeed;
        p.startSpeedRandomness = mainData.startSpeedRandomness;
        p.startColorRandomness = mainData.startColorRandomness;
        p.shapeEnabled = shapeModule_->IsEnabled() ? 1u : 0u;
        p.velocityEnabled = velocityModule_->IsEnabled() ? 1u : 0u;
        p.velocityUseRandomDir = velocityData.useRandomDirection ? 1u : 0u;

        // ===== Shape =====
        p.shapeScale = shapeData.scale;
        p.shapeType = static_cast<uint32_t>(shapeData.shapeType);
        p.shapeRadius = shapeData.radius;
        p.shapeInnerRadius = shapeData.innerRadius;
        p.shapeHeight = shapeData.height;
        p.shapeAngleRad = shapeData.angle * kDegToRad;
        p.shapeEmissionDirection = shapeData.emissionDirection;
        p.shapeRandomPositionRange = shapeData.randomPositionRange;
        p.shapeCirclePlane = static_cast<uint32_t>(shapeData.circlePlane);
        p.shapeEmitFromSurface = shapeData.emitFromSurface ? 1u : 0u;

        // ===== Velocity =====
        p.velocityDirection = velocityData.startSpeed;
        p.velocityRandomRange = velocityData.randomSpeedRange;

        // ===== Force =====
        p.gravity = forceData.gravity;
        p.drag = forceData.drag;
        p.wind = forceData.wind;
        p.forceEnabled = forceModule_->IsEnabled() ? 1u : 0u;
        p.fieldAcceleration = forceData.acceleration;
        p.useAccelerationField = forceData.useAccelerationField ? 1u : 0u;
        p.fieldMin = forceData.area.min;
        p.fieldMax = forceData.area.max;

        // ===== Color =====
        p.endColor = colorData.endColor;
        p.colorEnabled = (colorModule_->IsEnabled() && colorData.useGradient) ? 1u : 0u;

        // ===== Size =====
        p.sizeEnabled = (sizeModule_->IsEnabled() && sizeData.sizeOverLifetime) ? 1u : 0u;
        p.sizeUse3D = sizeData.use3DSize ? 1u : 0u;
        p.sizeCurve = static_cast<uint32_t>(sizeData.sizeCurve);
        p.endSize3D = sizeData.endSize3D;
        p.endSize = sizeData.endSize;
        p.sizeMin = sizeData.minSize;
        p.sizeMax = sizeData.maxSize;

        // ===== Rotation =====
        p.rotationEnabled = rotationModule_->IsEnabled() ? 1u : 0u;
        p.rotationUse2D = rotationData.use2DRotation ? 1u : 0u;
        p.rotationSpeed3D = rotationData.rotationSpeed;
        p.rotation2DSpeed = rotationData.rotation2DSpeed;
        p.rotationSpeedRandomness3D = rotationData.rotationSpeedRandomness;
        p.rotation2DSpeedRandomness = rotationData.rotation2DSpeedRandomness;
        p.rotationDirectionMode = static_cast<uint32_t>(rotationData.rotationDirection);
        p.rotationOverLifetime = rotationData.rotationOverLifetime ? 1u : 0u;
        p.rotStartMul = rotationData.startRotationSpeedMultiplier;
        p.rotEndMul = rotationData.endRotationSpeedMultiplier;

        // ===== Noise =====
        p.noisePositionAmount = noiseData.positionAmount;
        p.noiseEnabled = noiseModule_->IsEnabled() ? 1u : 0u;
        p.noiseStrength = noiseData.strength;
        p.noiseFrequency = noiseData.frequency;
        p.noiseScrollSpeed = noiseData.scrollSpeed;
        p.noiseDamping = noiseData.damping ? 1u : 0u;

        // 放出・更新のディスパッチと描画はレンダラーが行う
        if (renderer_) {
            renderer_->DrawGpu(this);
        }
    }

    bool GpuParticleSystemComponent::LoadPreset(const std::string& filePath)
    {
        return presetManager_->LoadPreset(this, filePath);
    }

    bool GpuParticleSystemComponent::SavePreset(const std::string& filePath)
    {
        return presetManager_->SavePreset(this, filePath);
    }

    void GpuParticleSystemComponent::SetTexture(const std::string& texturePath)
    {
        textureAsset_.SetPath(texturePath);
        LoadTexture();
    }

    void GpuParticleSystemComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) { return; }
        textureAsset_.SetValue(value);
        LoadTexture();
    }

    void GpuParticleSystemComponent::LoadTexture()
    {
        // Awake より前は指す先だけを控え、Awake で読み込む
        if (!awoken_) { return; }
        texture_ = TextureManager::GetInstance().Load(
            textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string(kDefaultTexture));
    }

    Vector3 GpuParticleSystemComponent::GetEmitterPosition() const
    {
        const GameObject* const owner = GetOwner();
        return owner ? owner->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
    }

    bool GpuParticleSystemComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const ITransformSource*>(&other) != nullptr;
    }

    json GpuParticleSystemComponent::OnSerialize() const
    {
        // ビルボードとブレンドは記述子が保存する
        json settings = ParticlePresetManager::ToJson(const_cast<GpuParticleSystemComponent&>(*this));
        settings.erase("billboardType");
        settings.erase("blendMode");
        return settings;
    }

    void GpuParticleSystemComponent::OnDeserialize(const json& j)
    {
        ParticlePresetManager::FromJson(*this, j);
    }
}
