#include "SkeletonModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Camera/ICamera.h"

using namespace CoreEngine;


void SkeletonModelObject::Initialize() {
    auto engine = GetEngineSystem();

    // ModelManagerを取得
    auto modelManager = engine->GetComponent<ModelManager>();
    if (!modelManager) {
        return;
    }

    // アニメーションを事前に読み込む
    AnimationLoadInfo animInfo;
    animInfo.directory = "SampleAssets/simpleSkin";
    animInfo.modelFilename = "simpleSkin.gltf";
    animInfo.animationName = "simpleSkinAnimation";
    animInfo.animationFilename = "simpleSkin.gltf";
    modelManager->LoadAnimation(animInfo);

    // スケルトンアニメーションモデルとして作成
    model_ = modelManager->CreateSkeletonModel(
        "SampleAssets/simpleSkin/simpleSkin.gltf",
        "simpleSkinAnimation",  // アニメーション名
        true   // ループ再生
    );

    // Transformの初期化
    auto dxCommon = engine->GetComponent<DirectXCommon>();
    if (dxCommon) {
        transform_.Initialize(dxCommon->GetDevice());
    }

    // 初期位置・スケール設定
    transform_.translate = { -3.0f, 0.0f, 0.0f };  // 左側に配置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };

    // テクスチャを初期化時に読み込む
    uvCheckerTexture_ = CoreEngine::TextureManager::GetInstance().Load("uvChecker.png");

    // アクティブ状態に設定
    SetActive(true);
}

void SkeletonModelObject::Update() {
    if (!IsActive() || !model_) {
        return;
    }

    auto engine = GetEngineSystem();

    // Transformの更新
    transform_.TransferMatrix();

    // FrameRateControllerから1フレームあたりの時間を取得
    auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
    if (!frameRateController) {
        return;
    }

    float deltaTime = frameRateController->GetDeltaTime();

    // アニメーションの更新（コントローラー経由で自動的にスケルトンも更新される）
    if (model_->HasAnimationController()) {
        model_->UpdateAnimation(deltaTime);
    }
}

void SkeletonModelObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;

    // モデルの描画
    model_->Draw(transform_, camera, uvCheckerTexture_.gpuHandle);
}

