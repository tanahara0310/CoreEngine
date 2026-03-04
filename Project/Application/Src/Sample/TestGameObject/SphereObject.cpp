#include "SphereObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

using namespace CoreEngine;

void SphereObject::Initialize() {
    // 必須コンポーネントの取得
    auto engine = GetEngineSystem();

    auto dxCommon = engine->GetComponent<DirectXCommon>();
    auto modelManager = engine->GetComponent<ModelManager>();

    if (!dxCommon || !modelManager) {
        return;
    }

    // 静的モデルとして作成
    model_ = modelManager->CreateStaticModel("SampleAssets/Sphere/sphere.obj");

    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());

    // テクスチャの読み込み
    auto& textureManager = CoreEngine::TextureManager::GetInstance();
    texture_ = textureManager.Load("Texture/white1x1.png");

    // アクティブ状態に設定
    SetActive(true);
}

void SphereObject::Update() {
    if (!IsActive() || !model_) {
        return;
    }

    // トランスフォームの更新
    transform_.TransferMatrix();
}

void SphereObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;

    // モデルの描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}

void SphereObject::SetMaterialColor(const Vector4& color) {
    defaultColor_ = color;
    if (model_) model_->GetMaterial()->SetColor(color);
}

void SphereObject::OnCollisionEnter([[maybe_unused]] GameObject* other) {
    if (++hitCount_ == 1) {
        model_->GetMaterial()->SetColor(hitColor_);
    }
}

void SphereObject::OnCollisionStay([[maybe_unused]] GameObject* other) {
    // Enter でカラー変更済みのため何もしない
}

void SphereObject::OnCollisionExit([[maybe_unused]] GameObject* other) {
    if (--hitCount_ <= 0) {
        hitCount_ = 0;
        model_->GetMaterial()->SetColor(defaultColor_);
    }
}

