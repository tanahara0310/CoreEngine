#include "pch.h"
#include "Particle/Gpu/GpuParticleSystemComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/ComponentService.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/Particle/GpuParticleRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Particle/Core/ParticleResourceManager.h" // ParticleForGPU（インスタンスデータレイアウト共有）
#include "Particle/ParticleBillboardNames.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace
{
    constexpr float kDegToRad = CoreEngine::MathCore::Constants::kDegToRad;
}

REFLECT_DEFINE_BEGIN(CoreEngine::GpuParticleSystemComponent, "GPU パーティクル")
    REFLECT_PARTIAL()
    REFLECT_JSON(SaveModulesToJson, LoadModulesFromJson)
    REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
        p.assetType = ::CoreEngine::AssetType::Texture, p.emptyText = &DescribeDefaultTexture)
    REFLECT_ENUM_ACCESSOR("blendMode", "ブレンド", GetBlendMode, SetBlendMode, ::CoreEngine::kBlendModeNames,
        p.tooltip = "背景との合成のしかた。炎や光は「加算」、煙は「アルファ」が向く")
    REFLECT_ENUM_ACCESSOR("billboard", "ビルボード", GetBillboardType, SetBillboardType, ::CoreEngine::kParticleBillboardNames,
        p.tooltip = "板ポリの向き。草や炎の柱のように立てたいものは「Y 軸だけ回す」")
    REFLECT_METHOD("Play", "再生", Play)
    REFLECT_METHOD("Stop", "停止", Stop)
    REFLECT_METHOD("IsPlaying", "再生中か", IsPlaying)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::GpuParticleSystemComponent)
COMPONENT_REGISTER(CoreEngine::GpuParticleSystemComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    GpuParticleSystemComponent::GpuParticleSystemComponent()
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
        if (readbackResource_ && readbackData_) {
            readbackResource_->Unmap(0, nullptr);
        }
        ReleaseGpuResources();
    }

    void GpuParticleSystemComponent::ReleaseGpuResources()
    {
        if (!graphics_) { return; }

        for (UniqueDescriptor* const descriptor : {
                &particleUavGPU_, &counterUavGPU_, &freeListUavGPU_, &instancingUavGPU_, &instancingSrvGPU_ }) {
            descriptor->Reset();
        }

        graphics_->DeferRelease(std::move(particleResource_));
        graphics_->DeferRelease(counterResource_.Get());
        graphics_->DeferRelease(freeListResource_.Get());
        graphics_->DeferRelease(instancingResource_.Get());
        graphics_->DeferRelease(argsResource_.Get());
        graphics_->DeferRelease(std::move(uploadInitResource_));
        graphics_->DeferRelease(std::move(readbackResource_));
        counterResource_.Release();
        freeListResource_.Release();
        instancingResource_.Release();
        argsResource_.Release();
        readbackData_ = nullptr;
        paramsAddress_ = 0;
        graphics_ = nullptr;
    }

    void GpuParticleSystemComponent::Awake()
    {
        GameObject* const owner = EnsureEmitterTransform();
        if (!owner) { return; }

        GraphicsCore* const graphics = FindService<GraphicsCore>(*this);
        RenderManager* const renderManager = FindService<RenderManager>(*this);
        if (!graphics || !renderManager) { return; }

        graphics_ = graphics;
        if (!CreateGpuResources(*graphics)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "GpuParticleSystemComponent: \"{}\" の GPU バッファを作れませんでした", owner->GetName());
            return;
        }
        renderer_ = renderManager->GetRendererAs<GpuParticleRenderer>(RenderPassType::GpuParticle);

        FinishAwake();
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
            const D3D12_RESOURCE_DESC desc = ResourceFactory::MakeBufferDesc(256); // D3D12_DRAW_ARGUMENTS(16B) を256バイトアライン
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
        {
            constexpr size_t kArgsIndex = kInitArgsOffset / sizeof(uint32_t);
            constexpr size_t kCountersIndex = kInitCountersOffset / sizeof(uint32_t);
            constexpr size_t kFreeListIndex = kInitFreeListOffset / sizeof(uint32_t);
            std::vector<uint32_t> initData(kFreeListIndex + kMaxParticles, 0u);
            initData[kArgsIndex] = 6u;                                        // ビルボード四角形の頂点数
            initData[kCountersIndex + kCounterFreeTopIndex] = kMaxParticles; // freeTop（alive・draw は 0）
            for (uint32_t i = 0; i < kMaxParticles; ++i) {
                initData[kFreeListIndex + i] = i;
            }
            uploadInitResource_ = ResourceFactory::CreateStaticBuffer<uint32_t>(device, initData);
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
        particleUavGPU_ = UniqueDescriptor(*descriptorAllocator, descriptorAllocator->CreateUAV(particleResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(GpuParticleData)), "GpuParticleUAV"));
        counterUavGPU_ = UniqueDescriptor(*descriptorAllocator, descriptorAllocator->CreateUAV(counterResource_.Get(),
            makeUavDesc(kCounterCount, sizeof(uint32_t)), "GpuParticleCounterUAV"));
        freeListUavGPU_ = UniqueDescriptor(*descriptorAllocator, descriptorAllocator->CreateUAV(freeListResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(uint32_t)), "GpuParticleFreeListUAV"));
        instancingUavGPU_ = UniqueDescriptor(*descriptorAllocator, descriptorAllocator->CreateUAV(instancingResource_.Get(),
            makeUavDesc(kMaxParticles, sizeof(ParticleForGPU)), "GpuParticleInstancingUAV"));
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = kMaxParticles;
            srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
            instancingSrvGPU_ = UniqueDescriptor(*descriptorAllocator,
                descriptorAllocator->CreateSRV(instancingResource_.Get(), srvDesc, "GpuParticleInstancingSRV"));
        }
        return true;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> GpuParticleSystemComponent::CreateUavBuffer(ID3D12Device* device, size_t sizeInBytes)
    {
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        const size_t alignedSize = (sizeInBytes + 255) & ~static_cast<size_t>(0xFF);

        const D3D12_RESOURCE_DESC resourceDesc =
            ResourceFactory::MakeBufferDesc(alignedSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if (FAILED(device->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)))) {
            return nullptr;
        }
        return buffer;
    }

    uint32_t GpuParticleSystemComponent::GetEffectiveCapacity() const
    {
        return std::clamp(mainModule_->GetData().maxParticles, 1u, kMaxParticles);
    }

    void GpuParticleSystemComponent::Update()
    {
        const float deltaTime = Time::DeltaTime();

        emitCountThisFrame_ = 0;
        ++frameSeed_;

        if (!mainModule_->IsEnabled()) {
            return;
        }

        const uint32_t emitCount = playback_.Advance(deltaTime, mainModule_->GetData(), emissionModule_->GetData());
        if (emissionModule_->IsEnabled()) {
            // 容量を超えた分は放出の CS が捨てるので、1 フレームの放出数は容量までにする
            emitCountThisFrame_ = (std::min)(emitCount, GetEffectiveCapacity());
        }
    }

    void GpuParticleSystemComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        const Camera* const camera = view.GetCamera();
        if (!owner || !owner->IsActive() || !camera || !IsAwoken()) {
            return;
        }

        const Matrix4x4 viewMatrix = camera->GetViewMatrix();
        const Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

        const auto& mainData = mainModule_->GetData();
        const auto& shapeData = shapeModule_->GetData();
        const auto& velocityData = velocityModule_->GetData();
        const auto& colorData = colorModule_->GetData();
        const auto& forceData = forceModule_->GetData();
        const auto& sizeData = sizeModule_->GetData();
        const auto& rotationData = rotationModule_->GetData();
        const auto& noiseData = noiseModule_->GetData();

        GpuParticleParams& p = params_;

        p.billboardMatrix = ParticleRenderDataBuilder::CreateBillboardMatrix(viewMatrix, billboardType_);
        p.viewProjection = viewMatrix * projectionMatrix;

        // ===== フレーム情報 =====
        p.emitterPosition = GetEmitterPosition();
        p.deltaTime = Time::DeltaTime();
        p.emitCount = emitCountThisFrame_;
        p.bufferCapacity = kMaxParticles;
        p.effectiveCapacity = GetEffectiveCapacity();
        p.frameSeed = frameSeed_;
        // GPU の初期化はレンダラーのコピーで行うので、CS の reset は常に 0
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

        // 放出・更新のディスパッチと描画はレンダラーが行う。定数はこのフレームの UploadRing に置く
        paramsAddress_ = graphics_->GetUploadRing().AllocateConstants(params_);
        if (renderer_) {
            renderer_->DrawGpu(this);
        }
    }
}
