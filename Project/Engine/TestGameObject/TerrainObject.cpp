#include "TerrainObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Camera/ICamera.h"
#include <numbers>


namespace CoreEngine
{
void TerrainObject::Initialize() {
   auto engine = GetEngineSystem();
   // 必須コンポーネントの取得
   auto dxCommon = engine->GetComponent<DirectXCommon>();
   auto modelManager = engine->GetComponent<ModelManager>();

   if (!dxCommon || !modelManager) {
	  return;
   }

   // 静的モデルとして作成
   model_ = modelManager->CreateStaticModel("SampleAssets/terrain/terrain.obj");

   // トランスフォームの初期化
   transform_.Initialize(dxCommon->GetDevice());

   // テレインの初期回転
   transform_.rotate = { 0.0f, std::numbers::pi_v<float> *0.5f, 0.0f };

   // テクスチャの読み込み
   auto& textureManager = TextureManager::GetInstance();
   texture_ = textureManager.Load("SampleAssets/terrain/grass.png");

   // アクティブ状態に設定
   SetActive(true);
}

void TerrainObject::Update() {
   if (!IsActive() || !model_) {
      return;
   }

   // トランスフォームの更新
   transform_.TransferMatrix();
}

void TerrainObject::Draw(const ICamera* camera) {
   if (!model_ || !camera) return;

   // モデルの描画
   model_->Draw(transform_, camera, texture_.gpuHandle);
}
}
