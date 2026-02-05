#pragma once

#include <memory>

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// パーティクルシステム
#include "Engine/Particle/ParticleSystem.h"

/// @brief パーティクルテスト専用シーンクラス

namespace CoreEngine
{
class ParticleTestScene : public BaseScene {
public:
    /// @brief 初期化
    void Initialize(EngineSystem* engine) override;

    /// @brief 描画
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

private:
    // パーティクルシステム
    ParticleSystem* particleSystem_ = nullptr;
};
}
