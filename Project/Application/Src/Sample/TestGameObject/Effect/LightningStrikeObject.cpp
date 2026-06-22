#include "pch.h"
#include "LightningStrikeObject.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace {
    using CoreEngine::Quaternion;
    using CoreEngine::Vector3;

    Vector3 SubtractVec3(const Vector3& a, const Vector3& b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    float DotVec3(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vector3 CrossVec3(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    float LengthVec3(const Vector3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    Vector3 NormalizeVec3(const Vector3& v) {
        const float length = LengthVec3(v);
        if (length <= 1.0e-5f) {
            return { 0.0f, -1.0f, 0.0f };
        }
        return { v.x / length, v.y / length, v.z / length };
    }

    Quaternion MakeQuaternionFromTo(const Vector3& from, const Vector3& to) {
        using namespace CoreEngine::MathCore;

        const Vector3 fromDir = NormalizeVec3(from);
        const Vector3 toDir = NormalizeVec3(to);
        const float dot = std::clamp(DotVec3(fromDir, toDir), -1.0f, 1.0f);

        if (dot >= 0.9999f) {
            return QuaternionMath::Identity();
        }

        Vector3 axis = CrossVec3(fromDir, toDir);
        if (LengthVec3(axis) <= 1.0e-5f) {
            axis = CrossVec3(fromDir, { 1.0f, 0.0f, 0.0f });
            if (LengthVec3(axis) <= 1.0e-5f) {
                axis = CrossVec3(fromDir, { 0.0f, 0.0f, 1.0f });
            }
        }
        axis = NormalizeVec3(axis);
        return QuaternionMath::MakeRotateAxisAngle(axis, std::acos(dot));
    }
}

LightningStrikeObject::LightningStrikeObject() {
    meshSettings_.seed = 2026;
    meshSettings_.segmentCount = 34;
    meshSettings_.branchCount = 0;
    meshSettings_.boltLength = 1.0f;
    meshSettings_.trunkJitter = 0.30f;
    meshSettings_.branchLengthMin = 0.48f;
    meshSettings_.branchLengthMax = 1.20f;
    meshSettings_.baseWidth = 0.055f;
    meshSettings_.tipWidth = 0.018f;

    lightningCB_.duration = 0.42f;
    lightningCB_.noiseTiling = 11.0f;
    lightningCB_.noiseScrollSpeed = 2.2f;
    lightningCB_.coreExponent = 4.4f;
    lightningCB_.haloExponent = 1.15f;
    lightningCB_.fadePower = 1.15f;
    lightningCB_.widthScale = 1.25f;
    lightningCB_.distortionAmplitude = 0.07f;
    lightningCB_.branchPulse = 1.45f;
    lightningCB_.shapeSeed0 = 0.13f;
    lightningCB_.shapeSeed1 = 0.41f;
    lightningCB_.shapeSeed2 = 0.67f;
    lightningCB_.shapeSeed3 = 0.89f;
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> LightningStrikeObject::CreateMeshGenerator() const {
    return std::make_unique<LightningBoltMeshGenerator>(meshSettings_);
}

void LightningStrikeObject::OnInitialize() {
    SetCustomShaderProvider(this);
    SetBlendMode(CoreEngine::BlendMode::kBlendModeAdd);

    auto* engine = GetEngineSystem();
    auto* dxCommon = engine ? engine->GetComponent<CoreEngine::DirectXCommon>() : nullptr;
    if (dxCommon) {
        CreateConstantBuffer(dxCommon->GetDevice());
    }

    ApplyMaterialSettings();

    GetTransform().SetRotationMode(CoreEngine::WorldTransform::RotationMode::Quaternion);
    UpdateTransformFromEndpoints();
    UploadConstants();
}

void LightningStrikeObject::OnUpdate() {
    auto* frameRate = GetEngineSystem() ? GetEngineSystem()->GetComponent<CoreEngine::FrameRateController>() : nullptr;
    const float deltaTime = frameRate ? frameRate->GetDeltaTime() : (1.0f / 60.0f);

    if (isPlaying_) {
        lightningCB_.elapsedTime += deltaTime;
        if (lightningCB_.elapsedTime >= lightningCB_.duration) {
            Stop();
            return;
        }
    }

    UploadConstants();
}

void LightningStrikeObject::CreateConstantBuffer(ID3D12Device* device) {
    assert(device);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (sizeof(LightningConstants) + 255) & ~255u;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    const HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&lightningCBResource_));
    assert(SUCCEEDED(hr));

    lightningCBGpuAddress_ = lightningCBResource_->GetGPUVirtualAddress();
    D3D12_RANGE readRange = { 0, 0 };
    lightningCBResource_->Map(0, &readRange, reinterpret_cast<void**>(&lightningCBMapped_));
}

void LightningStrikeObject::UploadConstants() {
    if (lightningCBMapped_) {
        std::memcpy(lightningCBMapped_, &lightningCB_, sizeof(LightningConstants));
    }
}

void LightningStrikeObject::BindCustomResources(
    ID3D12GraphicsCommandList* cmdList,
    const CoreEngine::CustomShaderPipeline* pipeline) const {
    if (!cmdList || !pipeline || lightningCBGpuAddress_ == 0) {
        return;
    }

    const int slot = pipeline->GetRootParamIndex("LightningConstants");
    if (slot >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(static_cast<UINT>(slot), lightningCBGpuAddress_);
    }
}

void LightningStrikeObject::SetStrikeEndpoints(const CoreEngine::Vector3& start, const CoreEngine::Vector3& end) {
    strikeStart_ = start;
    strikeEnd_ = end;
    UpdateTransformFromEndpoints();
}

void LightningStrikeObject::Trigger(float duration, float intensity) {
    auto& rng = CoreEngine::RandomGenerator::GetInstance();

    lightningCB_.elapsedTime = 0.0f;
    lightningCB_.duration = std::max(duration * rng.GetFloat(0.92f, 1.35f), 1.0e-3f);
    lightningCB_.intensity = std::max(intensity * rng.GetFloat(0.92f, 1.18f), 0.0f);
    lightningCB_.active = 1.0f;
    lightningCB_.noiseTiling = rng.GetFloat(8.5f, 13.5f);
    lightningCB_.noiseScrollSpeed = rng.GetFloat(1.6f, 3.4f);
    lightningCB_.coreExponent = rng.GetFloat(3.8f, 5.4f);
    lightningCB_.haloExponent = rng.GetFloat(0.95f, 1.35f);
    lightningCB_.fadePower = rng.GetFloat(0.95f, 1.35f);
    lightningCB_.widthScale = rng.GetFloat(1.10f, 1.55f);
    lightningCB_.distortionAmplitude = rng.GetFloat(0.05f, 0.09f);
    lightningCB_.branchPulse = rng.GetFloat(1.20f, 1.90f);
    lightningCB_.shapeSeed0 = rng.GetFloat(0.0f, 1.0f);
    lightningCB_.shapeSeed1 = rng.GetFloat(0.0f, 1.0f);
    lightningCB_.shapeSeed2 = rng.GetFloat(0.0f, 1.0f);
    lightningCB_.shapeSeed3 = rng.GetFloat(0.0f, 1.0f);
    isPlaying_ = true;
    UploadConstants();
}

void LightningStrikeObject::Stop() {
    isPlaying_ = false;
    lightningCB_.active = 0.0f;
    lightningCB_.elapsedTime = lightningCB_.duration;
    UploadConstants();
}

void LightningStrikeObject::SetNoiseParameters(float tiling, float scrollSpeed) {
    lightningCB_.noiseTiling = std::max(tiling, 0.01f);
    lightningCB_.noiseScrollSpeed = scrollSpeed;
    UploadConstants();
}

void LightningStrikeObject::SetGlowShape(float coreExponent, float haloExponent, float fadePower) {
    lightningCB_.coreExponent = std::max(coreExponent, 0.1f);
    lightningCB_.haloExponent = std::max(haloExponent, 0.1f);
    lightningCB_.fadePower = std::max(fadePower, 0.1f);
    UploadConstants();
}

void LightningStrikeObject::SetWidthScale(float widthScale) {
    lightningCB_.widthScale = std::max(widthScale, 0.01f);
    UploadConstants();
}

float LightningStrikeObject::GetNormalizedAge() const {
    if (lightningCB_.duration <= 1.0e-5f) {
        return 1.0f;
    }
    return std::clamp(lightningCB_.elapsedTime / lightningCB_.duration, 0.0f, 1.0f);
}

void LightningStrikeObject::RebuildBoltMesh() {
    auto* engine = GetEngineSystem();
    auto* modelMgr = engine ? engine->GetComponent<CoreEngine::ModelManager>() : nullptr;
    auto* dxCommon = engine ? engine->GetComponent<CoreEngine::DirectXCommon>() : nullptr;
    if (!modelMgr) {
        return;
    }

    auto generator = CreateMeshGenerator();
    if (!generator) {
        return;
    }

    model_ = modelMgr->CreatePrimitiveModel(generator->GetCacheKey(), *generator);
    ApplyMaterialSettings();
    BuildCustomShaderPipelineIfNeeded(dxCommon ? dxCommon->GetDevice() : nullptr, modelMgr);
}

void LightningStrikeObject::ApplyMaterialSettings() {
    if (auto* model = GetModel()) {
        if (auto* material = model->GetMaterial()) {
            material->SetColor({ 0.56f, 0.74f, 1.00f, 0.90f });
            material->SetLightingEnabled(false);
            material->SetMetallic(0.0f);
            material->SetRoughness(0.0f);
            material->SetAO(1.0f);
            material->SetDitheringEnabled(false);
            material->SetIBLIntensity(0.0f);
        }
    }
}

void LightningStrikeObject::UpdateTransformFromEndpoints() {
    auto& transform = GetTransform();
    const Vector3 direction = SubtractVec3(strikeEnd_, strikeStart_);
    const float length = std::max(LengthVec3(direction), 1.0e-3f);

    transform.translate = strikeStart_;
    transform.scale = { 1.0f, length, 1.0f };
    transform.quaternionRotate = MakeQuaternionFromTo({ 0.0f, -1.0f, 0.0f }, direction);
}
