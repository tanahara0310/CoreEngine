#include "pch.h"
#include "GpuParticleSystem.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Camera/Camera.h"
#include "Particle/Core/ParticleResourceManager.h" // ParticleForGPU（インスタンスデータレイアウト共有）
#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#include "Particle/Debug/ParticleSystemDebugUI.h"
#endif

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

namespace {
    constexpr float kDegToRad = MathCore::Constants::kDegToRad;
}

void GpuParticleSystem::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory, const std::string& name)
{
    if (!name.empty()) {
        name_ = name;
    }

    dxCommon_ = dxCommon;
    resourceFactory_ = resourceFactory;

    auto device = dxCommon_->GetDevice();
    auto descriptorManager = dxCommon_->GetDescriptorManager();

    // ──────────────────────────────────────────────────────────
    // モジュールの生成（CPU版とパラメータ定義・ImGuiを共有）
    // ──────────────────────────────────────────────────────────

    mainModule_ = std::make_unique<MainModule>();
    emissionModule_ = std::make_unique<EmissionModule>();
    shapeModule_ = std::make_unique<ShapeModule>();
    velocityModule_ = std::make_unique<VelocityModule>();
    colorModule_ = std::make_unique<ColorModule>();
    forceModule_ = std::make_unique<ForceModule>();
    sizeModule_ = std::make_unique<SizeModule>();
    rotationModule_ = std::make_unique<RotationModule>();
    noiseModule_ = std::make_unique<NoiseModule>();

    // GPUバックエンドであることをモジュールへ通知（ImGuiでCPU専用項目を隠す）
    {
        ParticleModule* modules[] = {
            mainModule_.get(), emissionModule_.get(), shapeModule_.get(),
            velocityModule_.get(), colorModule_.get(), forceModule_.get(),
            sizeModule_.get(), rotationModule_.get(), noiseModule_.get(),
        };
        for (auto* module : modules) {
            module->SetGpuBackend(true);
        }
    }

    // ImGuiの最大パーティクル数スピンをバッファ容量までに制限
    mainModule_->SetCapacityLimit(kMaxParticles);

    // ──────────────────────────────────────────────────────────
    // UAVバッファの作成（DEFAULTヒープ）
    // ──────────────────────────────────────────────────────────

    particleResource_ = CreateUavBuffer(sizeof(GpuParticleData) * kMaxParticles);
    counterResource_ = CreateUavBuffer(sizeof(uint32_t) * kCounterCount);
    freeListResource_ = CreateUavBuffer(sizeof(uint32_t) * kMaxParticles);
    instancingResource_ = CreateUavBuffer(sizeof(ParticleForGPU) * kMaxParticles);
    instancingState_ = D3D12_RESOURCE_STATE_COMMON;
    counterState_ = D3D12_RESOURCE_STATE_COMMON;
    argsState_ = D3D12_RESOURCE_STATE_COMMON;

    // 間接引数バッファ（ExecuteIndirect 用、UAV不要・コピー先/間接引数のみ）
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
        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&argsResource_));
        if (FAILED(hr)) {
            throw std::runtime_error("GpuParticleSystem: Failed to create indirect args buffer");
        }
    }

    // 初期化用アップロードバッファ（CopyBufferRegion のコピー元。ヘッダのオフセット定数参照）
    // [0..15]  間接引数 {VertexCountPerInstance=6, InstanceCount=0, 0, 0}
    //          （offset 4 は 0 なので drawCount の毎フレーム0クリアのコピー元としても使う）
    // [16..31] カウンタ初期値 {freeTop=kMaxParticles, alive=0, draw=0, 0}
    // [32..]   フリーリスト初期値 {0, 1, ..., kMaxParticles-1}
    // パーティクルバッファ自体はコミットリソースのOSゼロ初期化（lifeTime=0=死亡）に依存する
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

    // ──────────────────────────────────────────────────────────
    // UAV / SRV の作成
    // ──────────────────────────────────────────────────────────

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = kMaxParticles;
        uavDesc.Buffer.StructureByteStride = sizeof(GpuParticleData);
        descriptorManager->CreateUAV(particleResource_.Get(), uavDesc, cpuHandle, particleUavGPU_, "GpuParticleUAV");
    }
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = kCounterCount;
        uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        descriptorManager->CreateUAV(counterResource_.Get(), uavDesc, cpuHandle, counterUavGPU_, "GpuParticleCounterUAV");
    }
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = kMaxParticles;
        uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        descriptorManager->CreateUAV(freeListResource_.Get(), uavDesc, cpuHandle, freeListUavGPU_, "GpuParticleFreeListUAV");
    }
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = kMaxParticles;
        uavDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
        descriptorManager->CreateUAV(instancingResource_.Get(), uavDesc, cpuHandle, instancingUavGPU_, "GpuParticleInstancingUAV");
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = kMaxParticles;
        srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
        descriptorManager->CreateSRV(instancingResource_.Get(), srvDesc, cpuHandle, instancingSrvGPU_, "GpuParticleInstancingSRV");
    }

    // ──────────────────────────────────────────────────────────
    // 定数バッファ（UPLOAD・永続Map）
    // ──────────────────────────────────────────────────────────

    paramsResource_ = ResourceFactory::CreateBufferResource(device, sizeof(GpuParticleParams));
    paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
    *paramsData_ = GpuParticleParams{};

    // デフォルトテクスチャ
    SetTexture("Textures/circle.png");

    // 起動時再生（CPU版 MainModule.playOnAwake と同じ既定動作）
    if (mainModule_->GetMainData().playOnAwake) {
        Play();
    }
}

void GpuParticleSystem::Play()
{
    isPlaying_ = true;
    elapsedTime_ = 0.0f;
    burstDone_ = false;
    emitAccumulator_ = 0.0f;
}

uint32_t GpuParticleSystem::GetEffectiveCapacity() const
{
    const uint32_t maxParticles = mainModule_->GetMainData().maxParticles;
    return std::clamp(maxParticles, 1u, kMaxParticles);
}

void GpuParticleSystem::Update()
{
    const float kDeltaTime = 1.0f / 60.0f;

    emitCountThisFrame_ = 0;
    ++frameSeed_;

    if (!isPlaying_ || !mainModule_->IsEnabled()) {
        return;
    }

    const auto& mainData = mainModule_->GetMainData();
    const auto& emissionData = emissionModule_->GetEmissionData();

    elapsedTime_ += kDeltaTime;

    // duration / looping の管理（CPU版 ParticleSystem::Update と同等の挙動）
    if (elapsedTime_ >= mainData.duration) {
        if (mainData.looping) {
            elapsedTime_ = std::fmod(elapsedTime_, mainData.duration);
            burstDone_ = false; // ループごとにバーストを再発火
        } else {
            // ワンショット: 放出は止めるが生存中の粒子はGPU側で更新され続ける
            return;
        }
    }

    // Rate over Time
    if (emissionModule_->IsEnabled()) {
        emitAccumulator_ += static_cast<float>(emissionData.rateOverTime) * kDeltaTime;
        emitCountThisFrame_ = static_cast<uint32_t>(emitAccumulator_);
        emitAccumulator_ -= static_cast<float>(emitCountThisFrame_);

        // バースト（ループ内で1回、burstTime 経過時に発火）
        if (emissionData.burstCount > 0 && !burstDone_ && elapsedTime_ >= emissionData.burstTime) {
            emitCountThisFrame_ += emissionData.burstCount;
            burstDone_ = true;
        }
    }

    // 1フレームの放出数は実効容量まで（超過分は Emit CS の容量チェックで棄却されるため無意味）
    emitCountThisFrame_ = (std::min)(emitCountThisFrame_, GetEffectiveCapacity());
}

void GpuParticleSystem::Draw(const Camera* camera)
{
    if (!camera || !paramsData_) {
        return;
    }

    const float kDeltaTime = 1.0f / 60.0f;

    Matrix4x4 viewMatrix = camera->GetViewMatrix();
    Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

    // ビルボード行列（CPU版 ParticleRenderDataBuilder::CreateBillboardMatrix と同じ数式）
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
        Matrix4x4 invView = Matrix::Inverse(viewMatrix);
        Vector3 cameraPos = { invView.m[3][0], invView.m[3][1], invView.m[3][2] };
        Vector3 horizontal = { cameraPos.x, 0.0f, cameraPos.z };
        float horizontalLength = std::sqrt(horizontal.x * horizontal.x + horizontal.z * horizontal.z);

        Vector3 forward, right;
        if (horizontalLength < 0.0001f) {
            forward = { 0.0f, 0.0f, 1.0f };
            right = { 1.0f, 0.0f, 0.0f };
        } else {
            forward = { horizontal.x / horizontalLength, 0.0f, horizontal.z / horizontalLength };
            right = { -forward.z, 0.0f, forward.x };
        }

        billboardMatrix.m[0][0] = right.x;   billboardMatrix.m[0][1] = right.y;   billboardMatrix.m[0][2] = right.z;
        billboardMatrix.m[1][0] = 0.0f;      billboardMatrix.m[1][1] = 1.0f;      billboardMatrix.m[1][2] = 0.0f;
        billboardMatrix.m[2][0] = forward.x; billboardMatrix.m[2][1] = forward.y; billboardMatrix.m[2][2] = forward.z;
        break;
    }
    case BillboardType::ScreenAligned:
    {
        Matrix4x4 invView = Matrix::Inverse(viewMatrix);
        for (int row = 0; row < 3; ++row) {
            billboardMatrix.m[row][0] = invView.m[row][0];
            billboardMatrix.m[row][1] = invView.m[row][1];
            billboardMatrix.m[row][2] = invView.m[row][2];
        }
        break;
    }
    case BillboardType::None:
    default:
        break; // 単位行列（回転はパーティクル自身の回転のみ）
    }

    const auto& mainData = mainModule_->GetMainData();
    const auto& shapeData = shapeModule_->GetShapeData();
    const auto& velocityData = velocityModule_->GetVelocityData();
    const auto& colorData = colorModule_->GetColorData();
    const auto& forceData = forceModule_->GetForceData();
    const auto& sizeData = sizeModule_->GetSizeData();
    const auto& rotationData = rotationModule_->GetRotationData();
    const auto& noiseData = noiseModule_->GetNoiseData();

    GpuParticleParams& p = *paramsData_;

    p.billboardMatrix = billboardMatrix;
    p.viewProjection = Matrix::Multiply(viewMatrix, projectionMatrix);

    // ===== フレーム情報 =====
    p.emitterPosition = emitterPosition_;
    p.deltaTime = kDeltaTime;
    p.emitCount = emitCountThisFrame_;
    p.bufferCapacity = kMaxParticles;
    p.effectiveCapacity = GetEffectiveCapacity();
    p.frameSeed = frameSeed_;
    // GPU初期化はレンダラーのコピーで行うため、CSのresetフラグは常に0
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
}

void GpuParticleSystem::SetTexture(const std::string& texturePath)
{
    texture_ = TextureManager::GetInstance().Load(texturePath);
}

Microsoft::WRL::ComPtr<ID3D12Resource> GpuParticleSystem::CreateUavBuffer(size_t sizeInBytes)
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
    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&buffer));

    if (FAILED(hr)) {
        throw std::runtime_error("GpuParticleSystem: Failed to create UAV buffer");
    }

    return buffer;
}

#ifdef USE_IMGUI
int GpuParticleSystem::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const
{
    if (maxTabs < 1) return 0;
    // CPU版と同じタブ構成。色はGPUバッジと同じオレンジで区別する
    outTabs[0] = { "obj.png", "GPUパーティクル設定", {1.0f,0.6f,0.25f,1.0f}, {1.0f,0.6f,0.25f,0.25f} };
    return 1;
}

bool GpuParticleSystem::DrawInspectorTabContent(int tabIndex)
{
    if (tabIndex != 0) return false;
    // CPU版と共通の開発UI（ParticleSystemDebugUI）で表示する
    return ParticleSystemDebugUI::ShowImGui(this);
}
#endif

}
