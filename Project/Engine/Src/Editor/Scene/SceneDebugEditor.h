#pragma once

#ifdef USE_IMGUI

#include "Editor/Scene/UndoRedoHistory.h"
#include "Editor/ImGui/Gizmo.h"
#include "Editor/ImGui/ObjectSelector.h"
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

        /// @brief Hierarchyパネル内容のみ描画（ImGui::Begin/Endなし、外部ウィンドウへの埋め込み用）
        void DrawHierarchyContent();

        /// @brief Inspectorパネル内容のみ描画（選択オブジェクトのプロパティ）
        void DrawInspectorContent();

        /// @brief 履歴をすべてクリア（シーン切り替え時）
        void ClearHistory();

        /// @brief エンジン常駐 UI から自分への参照を外す
        /// @note 外さずに破棄すると、次のフレームで解放済みの this が呼ばれる
        void DetachFromEngineUI();

        /// @brief シーンオブジェクト（3D/スプライト）が選択中かどうか
        bool HasSelection() const {
            return objectSelector_.GetSelectedObject() != nullptr
                || objectSelector_.GetSelectedSprite() != nullptr;
        }

        /// @brief シーンオブジェクトの選択を解除する
        void ClearSelection() { objectSelector_.ClearSelection(); }

        /// @brief 選択中のシーンオブジェクトを取得する
        GameObject* GetSelectedObject() const { return objectSelector_.GetSelectedObject(); }

        /// @brief シーンオブジェクトを選択する
        /// @details Canvas ビューでの選択を Hierarchy / Inspector と共有するための入口。
        ///          ここを通さないと「Canvas で掴んだ要素と Inspector の表示が食い違う」
        void SelectObject(GameObject* object) { objectSelector_.SelectObject(object); }

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

        /// @brief 選択中オブジェクトをコピーしてシーンに追加する
        /// @return コピーに成功した場合 true
        bool CopySelectedObject();

        /// @brief モデルファイルをシーンにスポーンする
        /// @param modelFileName モデルファイル名（例: "cube.obj"）
        void SpawnModelFromFile(const std::string& modelFileName, const Vector2* normalizedDropPos = nullptr);

        /// @brief Gameビュー上の選択とギズモ描画を更新する
        /// @param viewportPos Gameビュー画像の左上座標
        /// @param viewportSize Gameビュー画像サイズ
        /// @param isViewportHovered Gameビュー画像がホバーされているか
        void UpdateGameViewportInteraction(
            const ImVec2& viewportPos,
            const ImVec2& viewportSize,
            bool isViewportHovered);

        /// @brief Gameビューへのモデルドロップを処理する
        /// @return モデルドロップを受理した場合 true
        bool AcceptGameViewportModelDrop(const ImVec2& viewportPos, const ImVec2& viewportSize);

        /// @brief 現在のギズモモードを取得する
        Gizmo::Mode GetGizmoMode() const;

        /// @brief ギズモモードを設定する
        void SetGizmoMode(Gizmo::Mode mode);

    private:
        UndoRedoHistory undoRedoHistory_;
        ObjectSelector objectSelector_;

        // 保存通知用
        std::string saveNotificationMessage_;
        double saveNotificationEndTime_ = 0.0;
        static constexpr double kNotificationDuration = 2.5;

        // 非所有参照
        EngineSystem* engine_ = nullptr;
        GameObjectManager* gameObjectManager_ = nullptr;
        CameraManager* cameraManager_ = nullptr;
        SceneSaveSystem* saveSystem_ = nullptr;
    };
}

#endif // USE_IMGUI
