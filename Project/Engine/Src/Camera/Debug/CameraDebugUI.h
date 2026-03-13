#pragma once

#ifdef _DEBUG

#include <string>
#include <vector>
#include <memory>
#include <Math/MathCore.h>
#include "Camera/CameraStructs.h"
#include "Camera/CameraPresetManager.h"
#include "CameraAnimationEditor.h"
#include "CameraPresetEditor.h"
#include "CameraInspectorEditor.h"

namespace CoreEngine {

    // 前方宣言
    class CameraManager;
    class ICamera;
    class DebugCamera;
    class Camera;

    /// @brief カメラデバッグUI - ImGuiを使用したカメラ制御インターフェース
    class CameraDebugUI {
    public:

        /// @brief コンストラクタ
        CameraDebugUI();

        /// @brief 初期化
        /// @param cameraManager カメラマネージャーへのポインタ
        void Initialize(CameraManager* cameraManager);

        /// @brief ImGuiウィンドウを描画
        void Draw();

    private:
        /// @brief 3Dカメラセクションを描画
        void DrawCamera3DSection();

        /// @brief 2Dカメラセクションを描画
        void DrawCamera2DSection();

    /// @brief プリセット管理セクションを描画
    void DrawPresetSection();

    /// @brief アニメーションセクションを描画
    void DrawAnimationSection();

private:
    CameraManager* cameraManager_ = nullptr;

    // プリセットのデータ管理
    CameraPresetManager presetManager_;

    // インスペクターUI責務を分離する。
    std::unique_ptr<CameraInspectorEditor> inspectorEditor_;

    // プリセットUI責務を分離する。
    std::unique_ptr<CameraPresetEditor> presetEditor_;

    // アニメーションUI/更新の責務を分離する。
    std::unique_ptr<CameraAnimationEditor> animationEditor_;
};

}

#endif // _DEBUG
