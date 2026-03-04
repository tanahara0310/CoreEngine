#pragma once

#ifdef _DEBUG

#include "Scene/UndoRedoHistory.h"
#include <string>

namespace CoreEngine
{
    class GameObjectManager;
    class EngineSystem;
    class CameraManager;
    class SceneSaveSystem;

    /// @brief デバッグ編集機能を管理するクラス（デバッグビルド専用）
    class SceneDebugEditor {
    public:
        /// @brief Undo/Redo コールバックと保存コールバックをセットアップする
        void Initialize(EngineSystem* engine, GameObjectManager* mgr,
                        CameraManager* camMgr, SceneSaveSystem* saveSystem);

        /// @brief デバッグ更新（カメラ・ショートカット・ ImGui ウィンドウ）
        void Update();

        /// @brief "オブジェクト制御" ImGui ウィンドウを描画する
        void DrawObjectControlWindow();

        /// @brief 履歴をすべてクリア（シーン切り替え時）
        void ClearHistory();

        bool Undo(GameObjectManager* mgr) { return undoRedoHistory_.Undo(mgr); }
        bool Redo(GameObjectManager* mgr) { return undoRedoHistory_.Redo(mgr); }
        bool CanUndo() const { return undoRedoHistory_.CanUndo(); }
        bool CanRedo() const { return undoRedoHistory_.CanRedo(); }
        int GetUndoCount() const { return undoRedoHistory_.GetUndoCount(); }
        int GetRedoCount() const { return undoRedoHistory_.GetRedoCount(); }

        /// @brief 保存完了通知を表示開始する
        void ShowSaveNotification(const std::string& message);

        /// @brief 保存通知オーバーレイを描画する
        void DrawSaveNotification();

    private:
        UndoRedoHistory undoRedoHistory_;

        // 保存通知用
        std::string saveNotificationMessage_;
        double saveNotificationEndTime_ = 0.0;
        static constexpr double kNotificationDuration = 2.5;

        // 非所有参照
        EngineSystem*      engine_            = nullptr;
        GameObjectManager* gameObjectManager_  = nullptr;
        CameraManager*     cameraManager_     = nullptr;
        SceneSaveSystem*   saveSystem_        = nullptr;
    };
}

#endif // _DEBUG
