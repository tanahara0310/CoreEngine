#pragma once

#ifdef CORE_EDITOR

#include "Editor/Scene/ObjectEditing.h"
#include "Editor/Scene/UndoRedoHistory.h"
#include "Editor/ImGui/Gizmo.h"
#include "Editor/ImGui/ObjectSelector.h"
#include "Scene/SceneSaveSystem.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

        /// @brief オブジェクトを選んでいたら選択を解除する（消す直前に呼ぶ）
        void Deselect(const GameObject& object);

        /// @brief 選択中のシーンオブジェクトを取得する
        GameObject* GetSelectedObject() const { return objectSelector_.GetSelectedObject(); }

        /// @brief シーンオブジェクトを選択する
        /// @details Canvas ビューでの選択を Hierarchy / Inspector と共有するための入口。
        ///          ここを通さないと「Canvas で掴んだ要素と Inspector の表示が食い違う」
        void SelectObject(GameObject* object) { objectSelector_.SelectObject(object); }

        bool Undo() { return undoRedoHistory_.Undo(); }
        bool Redo() { return undoRedoHistory_.Redo(); }
        bool CanUndo() const { return undoRedoHistory_.CanUndo(); }
        bool CanRedo() const { return undoRedoHistory_.CanRedo(); }

        /// @brief シーンとカメラの構図を保存する
        /// @return 保存したら true（シーン名が無い・再生中・外の変更とぶつかるなら false）
        /// @note 再生中は保存せず、ステータスバーに理由を出す。
        ///       外の変更を消してしまうファイルがあれば保存せず、そのファイル名を控える（GetSaveConflicts）。
        bool SaveScene();

        /// @brief 外の変更とぶつかるファイルも、自分の値で上書きして保存する
        /// @return 保存したら true
        bool SaveSceneOverwriting();

        /// @brief 保存しようとして外の変更とぶつかったファイル名（無ければ空）
        const std::vector<std::string>& GetSaveConflicts() const { return saveConflicts_; }

        /// @brief ぶつかった保存をやめる
        void CancelSaveConflicts() { saveConflicts_.clear(); }

        /// @brief 保存していない変更があるときに、外で変わったシーンのファイル（無ければ空）
        /// @note 保存していない変更が無ければ、見つけた時点で読み直すのでここには入らない。
        const std::vector<SceneSaveSystem::ExternalChange>& GetExternalChanges() const { return externalChanges_; }

        /// @brief 外の変更を取り込むために、開いているシーンを読み直す（保存していない変更は捨てる）
        void ReloadFromDisk();

        /// @brief 外の変更を知ったうえで、読み直さずに続ける（同じ変更はもう知らせない）
        void KeepEditingDespiteExternalChanges();

        /// @brief 最後の保存から編集したか
        bool IsSceneDirty() const;

        /// @brief 操作をしていなくても、保存するまで未保存の変更があることにする
        /// @note 未保存の変更があったシーンを、再生の前の控えから組み直したときに使う。
        void MarkSceneDirty() { dirtyWithoutEdits_ = true; }

        /// @brief シーンのカメラ一式（無ければ nullptr）
        CameraManager* GetCameraManager() const { return cameraManager_; }

        /// @brief 開いているシーンの名前（無ければ空）
        std::string GetSceneName() const;
        int GetUndoCount() const { return undoRedoHistory_.GetUndoCount(); }
        int GetRedoCount() const { return undoRedoHistory_.GetRedoCount(); }

        /// @brief 保存完了通知を表示開始する
        void ShowSaveNotification(const std::string& message);

        /// @brief 保存通知オーバーレイを描画する
        void DrawSaveNotification();

        /// @brief 空のオブジェクトを作って選ぶ（Undo に積む）
        void CreateEmptyObject();

        /// @brief UI のオブジェクトを画面の中央に作って選ぶ（Undo に積む）
        void CreateUIObject(ObjectEditing::UIElementKind kind);

        /// @brief パーティクルのオブジェクトを Game ビューの中央の地面に作って選ぶ（Undo に積む）
        void CreateParticleObject(ObjectEditing::ParticleKind kind);

        /// @brief プレハブから作ったオブジェクトの今の構成と値を、プレハブへ書き戻す（Undo に積む）
        /// @return 書き戻したら true
        bool ApplyToPrefab(GameObject& object);

        /// @brief 選択中のオブジェクトを複製・削除できるか
        /// @param reason できないときの理由を書く先（要らなければ nullptr）
        bool CanEditSelectedObject(std::string* reason = nullptr) const;

        /// @brief 選択中のオブジェクトを複製して選ぶ（Undo に積む）
        /// @return 複製したら true
        bool DuplicateSelectedObject();

        /// @brief 選択中のオブジェクトを消す（Undo に積む）
        /// @return 消したら true
        bool DeleteSelectedObject();

        /// @brief 複製（Ctrl+D）と削除（Del）のショートカットを受ける
        /// @note Hierarchy と Game ビューの窓の中から呼ぶ。その窓にフォーカスがあるときだけ効く。
        void HandleSelectionShortcuts();

        /// @brief モデルファイルをシーンにスポーンする
        /// @param modelFileName モデルファイル名（例: "cube.obj"）
        void SpawnModelFromFile(const std::string& modelFileName, const Vector2* normalizedDropPos = nullptr);

        /// @brief プレハブからオブジェクトを作ってシーンに置く（Undo に積む）
        /// @param prefabFileName プレハブのファイル名（例: "Rock.prefab"）
        void SpawnPrefabFromFile(const std::string& prefabFileName, const Vector2* normalizedDropPos = nullptr);

        /// @brief Gameビュー上の選択とギズモ描画を更新する
        /// @param viewportPos Gameビュー画像の左上座標
        /// @param viewportSize Gameビュー画像サイズ
        /// @param isViewportHovered Gameビュー画像がホバーされているか
        void UpdateGameViewportInteraction(
            const ImVec2& viewportPos,
            const ImVec2& viewportSize,
            bool isViewportHovered);

        /// @brief Gameビューへのモデル・プレハブのドロップを処理する
        /// @return ドロップを受理した場合 true
        bool AcceptGameViewportModelDrop(const ImVec2& viewportPos, const ImVec2& viewportSize);

        /// @brief 現在のギズモモードを取得する
        Gizmo::Mode GetGizmoMode() const;

        /// @brief ギズモモードを設定する
        void SetGizmoMode(Gizmo::Mode mode);

    private:
        /// @brief Gameビュー上のドロップ位置から、置く場所（y = 0 の地面との交点）を求める
        /// @param normalizedDropPos Gameビュー内の位置（0〜1）。nullptr なら中央
        Vector3 ComputeDropPosition(const Vector2* normalizedDropPos) const;

        /// @brief オブジェクトの作成・複製・削除に渡す文脈（削除の前に選択を外す）
        ObjectEditing::Context MakeObjectEditingContext();

        /// @brief Hierarchy の 1 行（種類の記号・名前・Prefab と AS の札）を描く
        /// @details 子を持つ行は折りたためる。子は続けてここから描く。
        void DrawHierarchyRow(GameObject& object);

        /// @brief 親子の対応をこのフレーム分だけ作る
        /// @details Transform の親を逆に辿れるようにする。親を持たないものが根になる。
        void BuildHierarchyLinks();

        /// @brief `object` が `descendant` の先祖か（送り先までの道を開くのに使う）
        bool IsAncestorOf(const GameObject& object, const GameObject& descendant) const;

        /// @brief 選んでいるオブジェクトへエディタのカメラを寄せる
        /// @details 大きさが分かるものはそれが収まる距離まで、分からないものは既定の距離。
        void FocusOnSelection();

        /// @brief Hierarchy の行の右クリックメニュー（プレハブとして保存・プレハブへ適用・つながりを外す）
        void DrawObjectContextMenu(GameObject& object);

        /// @brief 再生中なら保存を断り、ステータスバーに理由を出す
        /// @return 再生中で断ったら true
        bool RefuseSaveWhilePlaying() const;

        /// @brief シーンとカメラの構図を書く
        /// @param overwrite 外の変更とぶつかるファイルも自分の値で書くか
        bool WriteScene(bool overwrite);

        /// @brief ステータスバーに知らせを出す
        void ShowStatus(const char* message, const ImVec4& color) const;

        /// @brief 外でシーンのファイルが変わったかを 1 秒ごとに調べる
        /// @details 保存していない変更が無ければその場で読み直し、あれば知らせる分として控える。
        void CheckExternalChanges();

        UndoRedoHistory undoRedoHistory_;
        ObjectSelector objectSelector_;

        // ヒエラルキーの親子（毎フレーム作り直す）
        std::unordered_map<const GameObject*, std::vector<GameObject*>> hierarchyChildren_;
        std::vector<GameObject*> hierarchyRoots_;

        // ビューポートで選び直したとき、その行まで送る相手
        GameObject* scrollTarget_ = nullptr;

        // 送りを続ける残りフレーム数。
        // 1 フレームで済ませると、折りたたみの開閉で一覧の高さが変わった直後に
        // 送り先が上限でクランプされて届かない（ImGui は前のフレームの高さで丸める）
        int scrollFramesLeft_ = 0;

        // 保存通知用
        std::string saveNotificationMessage_;
        double saveNotificationEndTime_ = 0.0;
        static constexpr double kNotificationDuration = 2.5;

        // 最後に保存したときの EditorCommandStack の通し番号
        uint64_t savedRevision_ = 0;

        // 操作をしていなくても未保存として扱うか（保存すると外れる）
        bool dirtyWithoutEdits_ = false;

        // 保存しようとして外の変更とぶつかったファイル名
        std::vector<std::string> saveConflicts_;

        // 保存していない変更があるときに見つけた、外で変わったファイル
        std::vector<SceneSaveSystem::ExternalChange> externalChanges_;

        // 「このまま続ける」を選んだときの外の変更（同じ変更はもう知らせない）
        std::vector<SceneSaveSystem::ExternalChange> acknowledgedChanges_;

        // 次に外の変更を調べる時刻
        std::chrono::steady_clock::time_point nextExternalCheck_{};

        // 非所有参照
        EngineSystem* engine_ = nullptr;
        GameObjectManager* gameObjectManager_ = nullptr;
        CameraManager* cameraManager_ = nullptr;
        SceneSaveSystem* saveSystem_ = nullptr;
    };
}

#endif // CORE_EDITOR
