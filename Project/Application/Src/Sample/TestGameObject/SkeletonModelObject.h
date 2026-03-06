#pragma once

#include "ObjectCommon/GameObject.h"
#include "Graphics/Model/Skeleton/SkeletonAnimator.h"
#include <memory>

/// @brief Skeletonモデルオブジェクト
class SkeletonModelObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief 描画パスタイプを取得（スキニングモデル用）
    /// @return 描画パスタイプ
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkinnedModel; }

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "SkeletonModel"; }

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

private:
    CoreEngine::TextureManager::LoadedTexture uvCheckerTexture_;
};

