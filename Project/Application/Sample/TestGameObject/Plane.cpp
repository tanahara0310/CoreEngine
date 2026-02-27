#include "Plane.h"

#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"

using namespace CoreEngine;


void Plane::Initialize()
{
    auto engine = GetEngineSystem();
    // 必須コンポーネントの取得
    auto dxCommon = engine->GetComponent<DirectXCommon>();
    auto modelManager = engine->GetComponent<ModelManager>();

    if (!dxCommon || !modelManager) {
        return;
    }

    // 静的モデルとして作成
    model_ = modelManager->CreateStaticModel("SampleAssets/plane/plane.obj");
    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());

    //テクスチャの読み込み
    auto& textureMgr = CoreEngine::TextureManager::GetInstance();
    texture_ = textureMgr.Load("Texture/white1x1.png");

}

void Plane::Update()
{
    // トランスフォームの更新
    transform_.TransferMatrix();
}

void Plane::Draw(const CoreEngine::ICamera* camera)
{
    //テクスチャ無しで描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}


