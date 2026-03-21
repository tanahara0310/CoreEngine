#include "ModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/ICamera.h"


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

CoreEngine::BlendMode ModelObject::GetBlendMode() const {
    if (model_) {
        auto* mat = model_->GetMaterial();
        // α < 1 かつディザリングOFF → アルファブレンドで段階的透明
        // ディザリングON の場合は kBlendModeNone のままディザリングに任せる
        if (mat && mat->GetColor().w < 1.0f && !mat->IsDitheringEnabled()) {
            return CoreEngine::BlendMode::kBlendModeNormal;
        }
    }
    return CoreEngine::BlendMode::kBlendModeNone;
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

void ModelObject::SetPBRTextureMapsEnabled(bool useNormal, bool useMetallic,
    bool useRoughness, bool useAO) {
    if (auto* mat = GetMaterial()) {
        mat->SetNormalMapEnabled(useNormal);
        mat->SetMetallicMapEnabled(useMetallic);
        mat->SetRoughnessMapEnabled(useRoughness);
        mat->SetAOMapEnabled(useAO);
    }
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

