#pragma once

#include "Camera/CameraStructs.h"
#include "Camera/Control/OrbitFlyController.h"

#include <string>
#include <vector>

/// @file
/// @brief シーンごとのカメラ状態（姿勢・投影・軌道操作）の保存と復元

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

        /// @brief シーン開始時に動かすカメラリグの名前（空なら動かさない）
        /// @details 「どのリグでこのシーンを撮るか」はシーンごとの決め事なので、
        ///          カメラの構図と同じファイルに置く。コードから
        ///          CameraRig::Activate を書かなくてもシーンが自分の画で始まる。
        std::string startupRigName;

        std::vector<CameraSceneStateEntry> cameras;
    };

    /// @brief シーンフォルダ内のカメラ状態ファイルを読み書きする
    ///
    /// @details
    /// エディタ視点カメラは CVar（CVars.json の d.SceneCamera.*）でも永続化されているが、
    /// あちらはプロジェクト全体で 1 つ。こちらはシーンごとに持ち、読み込みが後に走るため
    /// 「シーンに保存があればそちらが勝つ」という関係になる。
    ///
    /// ゲーム視点カメラの構図はこれまでどこにも保存されておらず、エディタで詰めても
    /// 再起動で消えていた。
    class CameraSceneStateIO {
    public:
        /// @brief シーン名からカメラ状態ファイルのパスを作る
        /// @details オブジェクトの保存先（Assets/Scenes/{scene}/）と同じ場所に置く。
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

        /// @brief シーン開始時に動かすリグの名前だけを読む
        /// @return 保存が無い / 指定が無ければ空文字
        /// @details カメラの姿勢は CameraFeature が、リグの起動は CameraRigFeature が
        ///          担当する。両方が同じファイルを丸ごと読む必要はないので入口を分けた。
        static std::string LoadStartupRigName(const std::string& sceneName);
    };
}
