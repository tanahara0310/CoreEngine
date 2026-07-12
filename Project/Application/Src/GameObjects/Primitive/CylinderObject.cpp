#include "pch.h"
#include "CylinderObject.h"

#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Primitive/CylinderMeshGenerator.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Math/MathCore.h"

#include <cmath>
#include <utility>

CylinderObject::CylinderObject(
    float topRadius,
    float bottomRadius,
    float height,
    uint32_t divisions,
    std::string texturePath)
    : topRadius_(topRadius)
    , bottomRadius_(bottomRadius)
    , height_(height)
    , divisions_(divisions)
    , texturePath_(std::move(texturePath)) {
}

const char* CylinderObject::GetObjectName() const {
    return "Cylinder";
}

void CylinderObject::SetDiscardThreshold(float threshold) {
    discardThreshold_ = threshold;
    if (auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr) {
        mat->SetAlphaCutoff(discardThreshold_);
    }
}

float CylinderObject::GetDiscardThreshold() const {
    return discardThreshold_;
}

void CylinderObject::SetUVScrollSpeed(const CoreEngine::Vector2& speed) {
    uvScrollSpeed_ = speed;
}

const CoreEngine::Vector2& CylinderObject::GetUVScrollSpeed() const {
    return uvScrollSpeed_;
}

std::wstring CylinderObject::GetVertexShaderPath() const {
    return L"Object3d.VS.hlsl";
}

std::wstring CylinderObject::GetPixelShaderPath() const {
    return L"Object3d.PS.hlsl";
}

D3D12_CULL_MODE CylinderObject::GetCullMode() const {
    return D3D12_CULL_MODE_NONE;
}

bool CylinderObject::GetDepthWriteEnable() const {
    return false;
}

void CylinderObject::OnInitialize() {
    SetCustomShaderProvider(this);
    // Deferred の不透明スキップ経路を避け、カスタムPSO（Cull None）で描画する
    SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);

    if (auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr) {
        // ディザリングが有効だとシェーダーが alphaCutoff 分岐に入らない
        mat->SetDitheringEnabled(false);
        mat->SetAlphaCutoff(discardThreshold_);
    }
    ApplyUVTransform();
}

void CylinderObject::OnUpdate() {
    auto* frameRateController =
        GetEngineSystem() ? GetEngineSystem()->GetComponent<CoreEngine::FrameRateController>() : nullptr;
    const float deltaTime = frameRateController ? frameRateController->GetDeltaTime() : 0.0f;

    uvOffset_.x = std::fmod(uvOffset_.x + uvScrollSpeed_.x * deltaTime, 1.0f);
    uvOffset_.y = std::fmod(uvOffset_.y + uvScrollSpeed_.y * deltaTime, 1.0f);

    ApplyUVTransform();
}

std::string CylinderObject::GetTexturePath() const {
    return texturePath_;
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CylinderObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::CylinderMeshGenerator>(
        topRadius_,
        bottomRadius_,
        height_,
        divisions_);
}

void CylinderObject::ApplyUVTransform() {
    auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
    if (!mat) {
        return;
    }

    CoreEngine::Matrix4x4 uvMat = CoreEngine::MathCore::Matrix::Identity();
    uvMat.m[3][0] = uvOffset_.x;
    uvMat.m[3][1] = uvOffset_.y;
    mat->SetUVTransform(uvMat);
}
