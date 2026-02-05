#include "FenceObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"


namespace CoreEngine
{
void FenceObject::Initialize() {
   auto engine = GetEngineSystem();
   // 必須コンポーネントの取得
   auto dxCommon = engine->GetComponent<DirectXCommon>();
   auto modelManager = engine->GetComponent<ModelManager>();

   if (!dxCommon || !modelManager) {
      return;
   }

   // 静的モデルとして作成
   model_ = modelManager->CreateStaticModel("SampleAssets/fence/fence.obj");

   // トランスフォームの初期化
   transform_.Initialize(dxCommon->GetDevice());

   // テクスチャの読み込み
   auto& textureManager = TextureManager::GetInstance();
   texture_ = textureManager.Load("SampleAssets/fence/fence.png");

   // アクティブ状態に設定
   SetActive(true);
}

void FenceObject::Update() {
   if (!IsActive() || !model_) {
      return;
   }

   // トランスフォームの更新
   transform_.TransferMatrix();
}

void FenceObject::Draw(const ICamera* camera) {
   if (!model_ || !camera) return;

   // モデルの描画
   model_->Draw(transform_, camera, texture_.gpuHandle);
}
}
