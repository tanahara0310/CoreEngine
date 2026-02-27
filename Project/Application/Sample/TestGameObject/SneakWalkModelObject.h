#pragma once

#include "Engine/ObjectCommon/GameObject.h"

/// @brief SneakWalkモデルオブジェクト
class SneakWalkModelObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;
    
#ifdef _DEBUG
    /// @brief ImGui拡張UI描画
    bool DrawImGuiExtended() override;
#endif

    /// @brief 描画パスタイプを取得（スキニングモデル用）
    /// @return 描画パスタイプ
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkinnedModel; }

#ifdef _DEBUG
    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "SneakWalkModel"; }
#endif

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

private:
    CoreEngine::TextureManager::LoadedTexture uvCheckerTexture_;  // テクスチャハンドル
};

