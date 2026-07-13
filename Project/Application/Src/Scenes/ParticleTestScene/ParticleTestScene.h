#pragma once

#include <memory>

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// パーティクルシステム（CPU/GPU共通インターフェース）
#include "Particle/IParticleSystem.h"


    /// @brief パーティクルシステムのテストシーン
class ParticleTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 描画
    void Draw() override;

protected:
    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

private:
    // パーティクルシステム（CPU/GPUとも共通インターフェースで保持）
    CoreEngine::IParticleSystem* particleSystem_ = nullptr;

    // GPUパーティクルシステム（CPU版との比較用）
    CoreEngine::IParticleSystem* gpuParticleSystem_ = nullptr;
};

