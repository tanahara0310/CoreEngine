#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

/// @brief プリミティブ描画のテストシーン
/// @note PlaneObject / PrimitiveSphereObject / CubeObject の動作確認用
class PrimitiveTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 描画
    void Draw() override;

protected:
    /// @brief 更新
    void OnUpdate() override;
};
