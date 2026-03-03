#include "ModelObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"

#ifdef _DEBUG
#include "Engine/Graphics/Material/Debug/MaterialDebugUI.h"
#endif


void ModelObject::Initialize(const std::string& modelPath) {
    // 必須コンポーネントの取得
    auto engine = GetEngineSystem();

    auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
    auto modelManager = engine->GetComponent<CoreEngine::ModelManager>();

    if (!dxCommon || !modelManager) {
        return;
    }

    // 静的モデルとして作成
    model_ = modelManager->CreateStaticModel(modelPath);

    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());

    // アクティブ状態に設定
    SetActive(true);
}

void ModelObject::Update() {
    if (!IsActive() || !model_) {
        return;
    }

    // トランスフォームの更新
    transform_.TransferMatrix();
}

void ModelObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;

    // モデルの描画（テクスチャハンドルを省略してモデル組み込みテクスチャを使用）
    model_->Draw(transform_, camera);
}

CoreEngine::MaterialInstance* ModelObject::GetMaterial() {
    return model_ ? model_->GetMaterial() : nullptr;
}

void ModelObject::SetPBRParameters(float metallic, float roughness, float ao) {
    if (auto* mat = GetMaterial()) {
        mat->SetMetallic(metallic);
        mat->SetRoughness(roughness);
        mat->SetAO(ao);
    }
}

void ModelObject::SetPBREnabled(bool enable) {
    if (auto* mat = GetMaterial()) mat->SetPBREnabled(enable);
}

void ModelObject::SetPBRTextureMapsEnabled(bool useNormal, bool useMetallic,
    bool useRoughness, bool useAO) {
    if (auto* mat = GetMaterial()) {
        mat->SetNormalMapEnabled(useNormal);
        mat->SetMetallicMapEnabled(useMetallic);
        mat->SetRoughnessMapEnabled(useRoughness);
        mat->SetAOMapEnabled(useAO);
    }
}

void ModelObject::SetEnvironmentMapEnabled(bool enable) {
    if (auto* mat = GetMaterial()) mat->SetEnvironmentMapEnabled(enable);
}

void ModelObject::SetEnvironmentMapIntensity(float intensity) {
    if (auto* mat = GetMaterial()) mat->SetEnvironmentMapIntensity(intensity);
}

void ModelObject::SetMaterialColor(const CoreEngine::Vector4& color) {
    if (auto* mat = GetMaterial()) mat->SetColor(color);
}

void ModelObject::SetIBLEnabled(bool enable) {
    if (auto* mat = GetMaterial()) mat->SetIBLEnabled(enable);
}

void ModelObject::SetIBLIntensity(float intensity) {
    if (auto* mat = GetMaterial()) mat->SetIBLIntensity(intensity);
}

void ModelObject::SetEnvironmentRotationY(float rotationY) {
    if (auto* mat = GetMaterial()) mat->SetEnvironmentRotationY(rotationY);
}

#ifdef _DEBUG
bool ModelObject::DrawImGuiExtended() {
    if (!model_) return false;
    if (!materialDebugUI_) materialDebugUI_ = std::make_unique<CoreEngine::MaterialDebugUI>();
    return materialDebugUI_->Draw(model_.get());
}
#endif
