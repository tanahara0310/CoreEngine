#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Graphics/Model/Skeleton/SkeletonAnimator.h"
#include <memory>

/// @brief Skeletonモデルオブジェクト

namespace CoreEngine
{
class SkeletonModelObject : public GameObject {
public:
    /// @brief 初期化処理
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const ICamera* camera) override;

    /// @brief 描画パスタイプを取得（スキニングモデル用）
    /// @return 描画パスタイプ
    RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }

#ifdef _DEBUG
    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "SkeletonModel"; }
#endif

    /// @brief トランスフォームを取得
    WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    Model* GetModel() { return model_.get(); }

private:
    /// @brief テクスチャハンドル
    TextureManager::LoadedTexture uvCheckerTexture_;
};
}
