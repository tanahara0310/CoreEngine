#include "SphereObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"


namespace CoreEngine
{
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
   auto& textureManager = TextureManager::GetInstance();
   texture_ = textureManager.Load("Texture/monsterBall.png");

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

void SphereObject::Draw(const ICamera* camera) {
   if (!camera || !model_) return;
   
   // モデルの描画
   model_->Draw(transform_, camera, texture_.gpuHandle);
}
}
