#include "WaterPlaneObject.h"

#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"
#include <cmath>

WaterPlaneObject::WaterPlaneObject(float size, uint32_t resolution,
    const std::string& albedoTextureName)
    : size_(size)
    , resolution_(resolution)
    , albedoTextureName_(albedoTextureName)
    , scrollSpeed_({ 0.03f, 0.01f })
    , uvTiling_({ 4.0f, 4.0f })
    , uvOffset_({ 0.0f, 0.0f }) {
}

void WaterPlaneObject::OnInitialize() {
    // 独自シェーダーを使用するよう登録する
    SetCustomShaderProvider(this);
}

void WaterPlaneObject::SetNormalMapTextureName(const std::string& fileName) {
    auto loaded = CoreEngine::TextureManager::GetInstance().Load(fileName);
    if (GetModel() && loaded.gpuHandle.ptr != 0) {
        GetModel()->SetNormalMapOverride(loaded.gpuHandle);
        GetModel()->GetMaterial()->SetNormalMapEnabled(true);
    }
}

void WaterPlaneObject::SetScrollSpeed(const CoreEngine::Vector2& speed) {
    scrollSpeed_ = speed;
}

void WaterPlaneObject::SetUVTiling(const CoreEngine::Vector2& tiling) {
    uvTiling_ = tiling;
}

void WaterPlaneObject::UpdateUVScroll(float deltaTime) {
    // UV オフセットを速度 × 時間で加算
    uvOffset_.x += scrollSpeed_.x * deltaTime;
    uvOffset_.y += scrollSpeed_.y * deltaTime;

    // 0〜1 の範囲内に折り返す（精度劣化防止）
    uvOffset_.x = std::fmod(uvOffset_.x, 1.0f);
    uvOffset_.y = std::fmod(uvOffset_.y, 1.0f);

    ApplyUVTransform();
}

std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> WaterPlaneObject::CreateMeshGenerator() const {
    return std::make_unique<CoreEngine::PlaneMeshGenerator>(
        size_, size_, resolution_, resolution_);
}

void WaterPlaneObject::ApplyUVTransform() {
    auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
    if (!mat) {
        return;
    }

    // UV 変換行列: Scale（タイリング） × Translate（スクロールオフセット）
    // HLSL 側は float4(uv, 0, 1) × uvTransform の行ベクトル乗算なので
    // 行列の m[0][0], m[1][1] がスケール、m[3][0], m[3][1] がオフセット
    CoreEngine::Matrix4x4 uvMat = CoreEngine::MathCore::Matrix::Identity();
    uvMat.m[0][0] = uvTiling_.x;
    uvMat.m[1][1] = uvTiling_.y;
    uvMat.m[3][0] = uvOffset_.x;
    uvMat.m[3][1] = uvOffset_.y;

    mat->SetUVTransform(uvMat);
}
