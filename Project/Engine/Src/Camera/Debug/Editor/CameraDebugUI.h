#pragma once

#ifdef _DEBUG

#include <string>
#include <vector>
#include <memory>
#include "Module/CameraEditorContext.h"
#include "Module/ICameraEditorModule.h"

namespace CoreEngine {

    // 前方宣言
    class CameraManager;
    class GameObjectManager;
    /// @brief カメラデバッグUI - ImGuiを使用したカメラ制御インターフェース
    class CameraDebugUI {
    public:

        /// @brief コンストラクタ
        CameraDebugUI();

        /// @brief 初期化
        /// @param cameraManager カメラマネージャーへのポインタ
        void Initialize(CameraManager* cameraManager);

        /// @brief 追従対象探索に使うオブジェクトマネージャーを設定
        /// @param gameObjectManager ゲームオブジェクトマネージャー
        void SetGameObjectManager(GameObjectManager* gameObjectManager) { gameObjectManager_ = gameObjectManager; }

        /// @brief ImGuiウィンドウを描画
        void Draw();

        /// @brief エディター機能モジュールを追加登録
        /// @param module 追加するモジュール
        void RegisterModule(std::unique_ptr<ICameraEditorModule> module);

        /// @brief モジュール登録をクリア
        void ClearModules();

    private:
        /// @brief 現在フレーム用のコンテキストを構築
        CameraEditorContext BuildContext();

    private:
        CameraManager* cameraManager_ = nullptr;
        GameObjectManager* gameObjectManager_ = nullptr;

        // 機能追加しやすいよう、エディター機能はモジュール列として管理する。
        std::vector<std::unique_ptr<ICameraEditorModule>> modules_;
    };

}

#endif // _DEBUG
