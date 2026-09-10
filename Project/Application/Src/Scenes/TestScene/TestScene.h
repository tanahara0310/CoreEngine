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
#include "Audio/AudioSystem.h"
#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "Camera/Camera.h"
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
#include "Camera/Shake/CameraShakeTypes.h"

// GameObjectのインクルード

using namespace Microsoft::WRL;

/// @brief テストシーンクラス

namespace CoreEngine
{
    class InputQuery;

    class TestScene : public BaseScene {
    public:
        /// @brief シーン固有の初期化
        void OnInitialize() override;

    protected:
        /// @brief 更新処理（BaseSceneのOnUpdate()をオーバーライド）
        void OnUpdate() override;

    private: // メンバ関数

        /// @brief カメラシェイクの動作確認（数字キーで各プリセットを再生する）
        /// @details 揺れの見え方はスクリーンショットでは分かりにくいので、
        ///          プリセットごとに実際に撃って目で確かめるための入口。
        void UpdateCameraShakeTest(const InputQuery& input);

    private: // メンバ変数

        Logger& logger = Logger::GetInstance();

        /// @brief 再生中の地震（無限に続くので止めるためにハンドルを持つ）
        ShakeHandle earthquakeHandle_ = 0;
    };
}
