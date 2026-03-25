#pragma once

#include "GameObject.h"
#include <memory>
#include <vector>
#include <deque>
#include <functional>
#include <map>
#include <string>

// Forward declaration
namespace CoreEngine {
    class RenderManager;
    class ICamera;
    class CollisionManager;
}

/// @brief すべてのGameObjectを一元管理するマネージャー
/// @note 更新、描画、削除を自動化し、使用者は登録とDestroyのみを意識する
namespace CoreEngine
{
    class GameObjectManager : public IObjectSpawner {
    public:
        /// @brief オブジェクトを登録（所有権を移動）
        /// @tparam T GameObjectの派生クラス
        /// @param obj 登録するオブジェクトのユニークポインタ
        /// @return 登録されたオブジェクトへの生ポインタ（操作用）
        template<typename T>
        T* AddObject(std::unique_ptr<T> obj) {
            static_assert(std::is_base_of_v<GameObject, T>, "T must derive from GameObject");
            return static_cast<T*>(SpawnRaw(std::move(obj)));
        }

        /// @brief IObjectSpawner実装: オブジェクトをスポーン（型消去後の共通経路）
        /// @note 追加が Update 中はプィーンディングに積む、フレーム終了後に確定する
        /// @param obj スポーンするオブジェクトの unique_ptr
        /// @return 登録されたオブジェクトへの生ポインタ
        GameObject* SpawnRaw(std::unique_ptr<GameObject> obj) override;

        /// @brief 全オブジェクトの更新処理
        /// @note autoUpdate_ が true のオブジェクトのみ更新されます
        /// @note 手動更新したい場合は obj->SetAutoUpdate(false) を設定後、自分で obj->Update() を呼んでください
        void UpdateAll();

        /// @brief 全オブジェクトをRenderManagerに登録して描画
        /// @param renderManager レンダーマネージャー
        void RegisterAllToRender(CoreEngine::RenderManager* renderManager);

        /// @brief フレーム終了時に削除マークされたオブジェクトを破棄
        /// @note 削除キューを使用して安全に破棄（GPU処理完了を考慮）
        void CleanupDestroyed();

        /// @brief 全オブジェクトをクリア（シーン終了時など）
        void Clear();

        /// @brief 登録されているオブジェクト数を取得
        /// @return オブジェクト数
        size_t GetObjectCount() const { return objects_.size(); }

        /// @brief 全オブジェクトのリストを取得（読み取り専用）
        /// @return オブジェクトリストの const 参照
        const std::deque<std::unique_ptr<GameObject>>& GetAllObjects() const { return objects_; }

        /// @brief コライダーを持つ全オブジェクトのコライダーを CollisionManager に登録
        /// @param collisionManager 登録先の CollisionManager
        void RegisterAllColliders(CollisionManager* collisionManager);

#ifdef _DEBUG
        /// @brief 全オブジェクトのImGuiデバッグUI表示
        void DrawAllImGui();
#endif

        /// @brief オブジェクトの値が ImGui で変更されたときのコールバックを設定
        void SetOnChangedCallback(std::function<void(GameObject*)> callback) {
            onChangedCallback_ = std::move(callback);
        }

        /// @brief 個別オブジェクト保存コールバックを設定
        void SetOnSaveRequestCallback(std::function<void(GameObject*)> callback) {
            onSaveRequestCallback_ = std::move(callback);
        }

#ifdef _DEBUG
        /// @brief ImGui 編集コミット時コールバックを設定（Undo/Redo 用）
        void SetEditCommitCallback(GameObject::EditCommitCallback cb) {
            editCommitCallback_ = std::move(cb);
        }
#endif

    private:
        /// @brief 管理中のオブジェクトリスト
        std::deque<std::unique_ptr<GameObject>> objects_;

        /// @brief 削除待ちキュー（フレーム終了後に破棄）
        std::vector<std::unique_ptr<GameObject>> destroyQueue_;

        /// @brief Update中のスポーン要求を蓄積するキュー（dequeイテレータ無効化を防ぐ）
        std::vector<std::unique_ptr<GameObject>> pendingAdd_;

        /// @brief UpdateAll()実行中フラグ（Spawn要求をpendingに誘導）
        bool isUpdating_ = false;

        /// @brief オブジェクト名の連番番号管理（名前重複を防ぐ）
        std::map<std::string, int> nameCounters_;

        /// @brief ImGui変更時コールバック（デバッグビルドのみ使用）
        std::function<void(GameObject*)> onChangedCallback_;

        /// @brief 個別オブジェクト保存リクエスト時コールバック
        std::function<void(GameObject*)> onSaveRequestCallback_;

        /// @brief pendingAdd_ を objects_ へ移動する
        void FlushPendingAdds();

#ifdef _DEBUG
        /// @brief ImGui 編集コミット時コールバック（Undo/Redo 用）
        GameObject::EditCommitCallback editCommitCallback_;
#endif
    };
}
