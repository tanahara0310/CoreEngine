#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObjects/Effect/HitEffect.h"

#include <memory>

/// @brief 課題専用シーン
/// @note 球体を複数配置し、ポストエフェクトにグレースケールを適用する
/// スペースキーで固定座標にヒットエフェクトを再生する
class HomeworkScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 描画
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新
    void OnUpdate() override;

private:
    std::unique_ptr<CoreEngine::HitEffect> hitEffect_;
};
