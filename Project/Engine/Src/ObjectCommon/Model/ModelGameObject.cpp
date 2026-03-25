#include "ModelGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Camera/ICamera.h"

namespace CoreEngine
{
    void ModelGameObject::Initialize() {
        auto* engine   = GetEngineSystem();
        auto* dxCommon = engine->GetComponent<DirectXCommon>();
        auto* modelMgr = engine->GetComponent<ModelManager>();

        // トランスフォームの初期化（GPU 定数バッファを確保）
        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        // モデルのロード（パスが空でなければ）
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty() && modelMgr) {
            model_ = modelMgr->CreateStaticModel(modelPath);
        }

        // テクスチャのロード（パスが空でなければ）
        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            texture_ = TextureManager::GetInstance().Load(texPath);
        }

        // 派生クラスの追加初期化（座標・コライダー設定など）
        OnInitialize();
        SetActive(true);
    }

    void ModelGameObject::Update() {
        if (!IsActive() || !model_) return;
        transform_.TransferMatrix();
        OnUpdate();
    }

    void ModelGameObject::Draw(const ICamera* camera) {
        if (!IsVisible() || !model_ || !camera) return;
        model_->Draw(transform_, camera, texture_.gpuHandle);
        OnDraw(camera);
    }

}  // namespace CoreEngine
