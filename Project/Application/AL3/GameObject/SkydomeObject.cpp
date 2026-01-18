#include "SkydomeObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Camera/ICamera.h"

void SkydomeObject::Initialize() {
    auto engine = GetEngineSystem();
    // 必須コンポーネントの取得
    auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
    auto modelManager = engine->GetComponent<CoreEngine::ModelManager>();

    if (!dxCommon || !modelManager) {
        return;
    }

    // 静的モデルとして作成（天球モデル）
    model_ = modelManager->CreateStaticModel("Assets/AppAssets/Skydome/SkyDome.obj");

    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());

    // 天球の初期設定（カメラのfarClipが1000なので、それより小さい値に設定）
    transform_.scale = { 500.0f, 500.0f, 500.0f };
    transform_.translate = { 0.0f, 0.0f, 0.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };

    // テクスチャの読み込み（天球用のテクスチャがある場合）
    auto& textureManager = CoreEngine::TextureManager::GetInstance();
    texture_ = textureManager.Load("Assets/AppAssets/Skydome/skyDome.png");

    // アクティブ状態に設定
    SetActive(true);
	transform_.TransferMatrix();
}

void SkydomeObject::Update() {
    if (!IsActive() || !model_) {
        return;
    }

    // トランスフォームの更新
    transform_.TransferMatrix();
}

void SkydomeObject::Draw(const CoreEngine::ICamera* camera) {
    if (!model_ || !camera) return;

    // モデルの描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}
