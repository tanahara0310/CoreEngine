#pragma once

#include <memory>

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// パーティクルシステム
#include "Particle/ParticleSystem.h"


    /// @brief パーティクルシステムのテストシーン
class ParticleTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 描画
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

private:
    // パーティクルシステム
    CoreEngine::ParticleSystem* particleSystem_ = nullptr;
};

