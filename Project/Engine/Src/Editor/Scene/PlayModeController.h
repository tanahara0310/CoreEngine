#pragma once

#ifdef CORE_EDITOR

#include "Camera/CameraSceneStateIO.h"

#include <cstddef>
#include <memory>
#include <string>

namespace CoreEngine
{
    class EngineSystem;
    class GameDebugUI;
    struct SceneSnapshot;
}

namespace CoreEngine::Editor
{
    /// @brief 再生の前にシーンを控え、停止したら控えから組み直す
    /// @details 停止では音を止めて時間の速さを戻す。CVar とエディタの視点・選択は戻さない。
    class PlayModeController
    {
    public:
        PlayModeController() = default;
        ~PlayModeController();

        PlayModeController(const PlayModeController&) = delete;
        PlayModeController& operator=(const PlayModeController&) = delete;

        /// @brief 再生の開始と停止に処理を差し込む
        /// @param gameDebugUI 今のシーンのエディタ（選択・保存状態・カメラ）を引く先
        void Initialize(EngineSystem* engine, GameDebugUI* gameDebugUI);

        /// @brief 差し込んだ処理を外す
        void Finalize();

        /// @brief 控えているオブジェクトの数（控えていなければ 0）
        std::size_t GetSnapshotObjectCount() const;

        /// @brief 控えるのにかかった秒数
        double GetCaptureSeconds() const { return captureSeconds_; }

        /// @brief 再生を始めてから進んだゲームの時間（秒。一時停止中は進まない）
        float GetPlayTime() const;

    private:
        /// @brief 再生を始める前に、シーンとカメラと保存状態を控える
        /// @return シーンの読み込みやトランジションの途中なら false（次のフレームで試す）
        bool CapturePlayStart();

        /// @brief 停止したときに、控えからシーンを組み直す
        /// @return シーンの読み込みやトランジションの途中なら false（次のフレームで試す）
        bool RestorePlayStart();

        /// @brief 再生中に鳴らした音を止め、時間の速さを再生前へ戻す
        void ResetPlaySession() const;

        EngineSystem* engine_ = nullptr;
        GameDebugUI* gameDebugUI_ = nullptr;

        /// 再生前のシーンの控え（控えていなければ空）
        std::shared_ptr<const SceneSnapshot> snapshot_;

        /// 控えたシーンの、SceneManager に登録した名前
        std::string sceneName_;

        /// 再生前のカメラ
        CameraSceneState cameraState_{};
        bool hasCameraState_ = false;

        /// 再生前に未保存の変更があったか
        bool sceneDirty_ = false;

        /// 再生前の時間の速さ
        float timeScale_ = 1.0f;

        /// 再生を始めたときのゲームの累積時間
        float playStartTime_ = 0.0f;

        /// 控えるのにかかった秒数
        double captureSeconds_ = 0.0;
    };
}

#endif // CORE_EDITOR
