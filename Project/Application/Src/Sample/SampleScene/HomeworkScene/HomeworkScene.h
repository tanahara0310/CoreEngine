#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
/// @brief 課題専用シーン
/// @note 球体を複数配置し、オフスクリーンのクリアカラーを赤、ポストエフェクトにグレースケールを適用する
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
};
