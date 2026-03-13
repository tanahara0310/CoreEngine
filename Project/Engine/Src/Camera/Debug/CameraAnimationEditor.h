#pragma once

#ifdef _DEBUG

#include "Camera/CameraPresetManager.h"

namespace CoreEngine
{
    class CameraManager;

    /// @brief カメラアニメーションのUI表示と再生更新を担当するクラス
    class CameraAnimationEditor {
    public:
        /// @brief アニメーションUIを描画
        /// @param presetManager プリセット管理
        /// @param cameraManager カメラ管理
        void DrawUI(CameraPresetManager& presetManager, CameraManager* cameraManager);

        /// @brief アニメーション再生更新
        /// @param cameraManager カメラ管理
        void Update(CameraManager* cameraManager);

    private:
        /// @brief アニメーション開始
        void StartAnimation(CameraPresetManager& presetManager, CameraManager* cameraManager, int fromIndex, int toIndex);

    private:
        int animFromFileIndex_ = 0;
        int animToFileIndex_ = 0;
        float animDuration_ = 1.0f;
        int easingTypeIndex_ = 0;  // 0=Linear, 1=EaseInOut, 2=EaseIn, 3=EaseOut

        struct AnimationState {
            bool isAnimating = false;
            float progress = 0.0f;
            float duration = 1.0f;
            CameraSnapshot fromSnapshot;
            CameraSnapshot toSnapshot;
        };
        AnimationState animation_;
    };
}

#endif // _DEBUG
