#include "AnimatedModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/FrameRate/FrameRateController.h"

namespace CoreEngine
{
    void AnimatedModelObject::Initialize() {
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine->GetComponent<DirectXCommon>();
        auto* modelMgr = engine->GetComponent<ModelManager>();

        // トランスフォームの初期化（GPU 定数バッファを確保）
        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        // アニメーションを先にロード（スケルトン生成前に必要）
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty() && modelMgr) {
            AnimationLoadInfo info;
            info.modelFile = modelPath;
            info.animationName = GetAnimationName();
            info.animationFile = GetAnimationFile();
            modelMgr->LoadAnimation(info);

            // スケルトンアニメーションモデルとして生成
            model_ = modelMgr->CreateSkeletonModel(modelPath, GetAnimationName(), true);
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

    void AnimatedModelObject::OnUpdate() {
        auto* fc = GetEngineSystem()->GetComponent<FrameRateController>();
        if (fc && model_ && model_->HasAnimationController()) {
            model_->UpdateAnimation(fc->GetDeltaTime());
        }
    }

}  // namespace CoreEngine
