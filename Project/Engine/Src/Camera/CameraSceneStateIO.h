#pragma once

#include "Camera/CameraStructs.h"
#include "Camera/Control/OrbitFlyController.h"

#include <string>
#include <vector>

/// @file
/// @brief シーンごとのエディタの視点（姿勢・投影・軌道操作）の保存と復元

namespace CoreEngine
{
    class CameraManager;

    /// @brief カメラ 1 台分の保存データ
    struct CameraSceneStateEntry {
        std::string name;
        CameraSnapshot snapshot{};

        /// @brief 軌道コントローラが付いていたか
        /// @details 付いているカメラは Transform を毎フレーム上書きされるため、
        ///          姿勢だけ戻しても次のフレームで消える。軌道状態のほうが正本。
        bool hasOrbitState = false;
        OrbitFlyController::OrbitState orbitState{};
    };

    /// @brief シーン 1 つ分のカメラ状態
    struct CameraSceneState {
        /// @brief エディタ視点として使うカメラ名
        std::string sceneCameraName;

        /// @brief ゲーム視点として使うカメラ名
        std::string gameCameraName;

        std::vector<CameraSceneStateEntry> cameras;
    };

    /// @brief シーンごとのエディタの視点を、自分だけの状態（`Application/Saved`）として読み書きする
    /// @note シーンのオブジェクトが持つカメラ（`CameraComponent`）は扱わない（構図はオブジェクトの保存データが持つ）。
    class CameraSceneStateIO {
    public:
        /// @brief シーン名からエディタの視点のファイルのパスを作る
        static std::string GetFilePath(const std::string& sceneName);

        /// @brief 現在のカメラ状態を集める
        static CameraSceneState Capture(const CameraManager& cameraManager);

        /// @brief カメラ状態をカメラマネージャーへ適用する
        /// @details 保存に無いカメラは触らない。シーン側が作った構図を消さないため。
        static void Apply(const CameraSceneState& state, CameraManager& cameraManager);

        /// @brief シーンのカメラ状態を保存する
        static bool Save(const std::string& sceneName, const CameraManager& cameraManager);

        /// @brief シーンのカメラ状態を読み込んで適用する
        /// @return ファイルが無い / 読めない場合は false（カメラは変更されない）
        static bool Load(const std::string& sceneName, CameraManager& cameraManager);
    };
}
