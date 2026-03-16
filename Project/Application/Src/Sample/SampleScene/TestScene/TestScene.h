#pragma once

#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <numbers>
#include <random>
#include <vector>
#include <memory>

// エンジンコア
#include "Audio/SoundManager.h"
#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Light/LightData.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Line/LineManager.h"

// シーン関連
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// IBL
#include "Graphics/IBL/IBLManager.h"

// GameObjectのインクルード
#include "Sample/TestGameObject/ModelObject.h"

using namespace Microsoft::WRL;

/// @brief テストシーンクラス

namespace CoreEngine
{
class TestScene : public BaseScene {
public:
    /// @brief 初期化
    void Initialize(EngineSystem* engine) override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
    void OnUpdate() override;

private: // メンバ変数

    Logger& logger = Logger::GetInstance();

    // IBL管理
    std::unique_ptr<IBLManager> iblManager_;
};
}
