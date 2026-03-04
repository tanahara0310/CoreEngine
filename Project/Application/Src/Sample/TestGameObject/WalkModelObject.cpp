#include "WalkModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/TextureManager.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Camera/ICamera.h"

#ifdef _DEBUG
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif


using namespace CoreEngine;

void WalkModelObject::Initialize() {
   auto engine = GetEngineSystem();

   // ModelManagerを取得
   auto modelManager = engine->GetComponent<ModelManager>();
   if (!modelManager) {
      return;
   }

   // アニメーションを事前に読み込む
   AnimationLoadInfo animInfo;
   animInfo.directory = "SampleAssets/human";
   animInfo.modelFilename = "walk.gltf";
   animInfo.animationName = "walkAnimation";
   animInfo.animationFilename = "walk.gltf";
   modelManager->LoadAnimation(animInfo);

   // スケルトンアニメーションモデルとして作成
   model_ = modelManager->CreateSkeletonModel(
      "SampleAssets/human/walk.gltf",
      "walkAnimation",  // アニメーション名
      true // ループ再生
   );

   // Transformの初期化
   auto dxCommon = engine->GetComponent<DirectXCommon>();
   if (dxCommon) {
      transform_.Initialize(dxCommon->GetDevice());
   }

   // 初期位置・スケール設定
   transform_.translate = { 3.0f, 0.0f, 0.0f };  // 右側に配置
   transform_.scale = { 1.0f, 1.0f, 1.0f };
   transform_.rotate = { 0.0f, 0.0f, 0.0f };

   // テクスチャを初期化時に読み込む
   texture_ = CoreEngine::TextureManager::GetInstance().Load("SampleAssets/human/white.png");

   // アクティブ状態に設定
   SetActive(true);
}

void WalkModelObject::Update() {
   if (!IsActive() || !model_) {
      return;
   }

   auto engine = GetEngineSystem();

   // Transformの更新
   transform_.TransferMatrix();

   // FrameRateControllerから1フレームあたりの時間を取得
   auto frameRateController = engine->GetComponent<FrameRateController>();
   if (!frameRateController) {
      return;
   }

   float deltaTime = frameRateController->GetDeltaTime();

   // アニメーションの更新（コントローラー経由で自動的にスケルトンも更新される）
   if (model_->HasAnimationController()) {
      model_->UpdateAnimation(deltaTime);
   }
}

void WalkModelObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;
    
    // モデルの描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}

#ifdef _DEBUG
bool WalkModelObject::DrawImGuiExtended() {
    if (!model_) return false;
    if (!materialDebugUI_) materialDebugUI_ = std::make_unique<CoreEngine::MaterialDebugUI>();
    return materialDebugUI_->Draw(model_.get());
}
#endif

