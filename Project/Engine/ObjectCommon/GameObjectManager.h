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
    class GameObjectManager {
    public:
        /// @brief オブジェクトを登録（所有権を移動）
        /// @tparam T GameObjectの派生クラス
        /// @param obj 登録するオブジェクトのユニークポインタ
        /// @return 登録されたオブジェクトへの生ポインタ（操作用）
        template<typename T>
        T* AddObject(std::unique_ptr<T> obj) {
            static_assert(std::is_base_of_v<GameObject, T>, "T must derive from GameObject");
            T* ptr = obj.get();
            // 名前未設定の場合は GetObjectName() + 連番番号で自動付与
            if (ptr->GetName().empty()) {
                std::string baseName = ptr->GetObjectName();
                int idx = nameCounters_[baseName]++;
                ptr->SetName(baseName + "_" + std::to_string(idx));
            }
            objects_.push_back(std::move(obj));
            return ptr;
        }

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

        /// @brief オブジェクト名の連番番号管理（名前重複を防ぐ）
        std::map<std::string, int> nameCounters_;

        /// @brief ImGui変更時コールバック（デバッグビルドのみ使用）
        std::function<void(GameObject*)> onChangedCallback_;

#ifdef _DEBUG
        /// @brief ImGui 編集コミット時コールバック（Undo/Redo 用）
        GameObject::EditCommitCallback editCommitCallback_;
#endif
    };
}
