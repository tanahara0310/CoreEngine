#pragma once

#include "GameObject.h"
#include "GameObject/ObjectId.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>

// Forward declaration
namespace CoreEngine {
    class RenderManager;
    class Camera;
    class CollisionWorld;
}
namespace CoreEngine
{
    /// @brief すべてのGameObjectを一元管理するマネージャー
    /// @note 更新、描画、削除を自動化し、使用者は登録とDestroyのみを意識する
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
        /// @param afterUpdatePass 全員の Update の後、ワールド行列を転送し直す前に呼ぶ処理（無くてよい）
        void UpdateAll(const std::function<void()>& afterUpdatePass = {});

        /// @brief 全オブジェクトのワールド行列だけを計算し直して GPU へ転送する
        /// @details `UpdateAll()` が全員の Update の後に呼び、Update の中で書き換えた座標を
        ///          そのフレームの描画と当たり判定に出す。
        ///          再生を停止している間は `UpdateAll()` の代わりに呼ぶ。ワールド行列の転送は
        ///          `TransformComponent::Update()` が担っているので、更新を丸ごと止めると
        ///          ギズモやインスペクタで座標を動かしても画面が変わらなくなる。
        /// @note 走査対象は `UpdateAll()` と同じ（非アクティブ・削除マーク済みは除く）。
        void SyncTransforms();

        /// @brief 全オブジェクトをRenderManagerに登録して描画
        /// @param renderManager レンダーマネージャー
        void RegisterAllToRender(CoreEngine::RenderManager* renderManager);

        /// @brief フレーム終了時に削除マークされたオブジェクトを破棄
        /// @note 削除キューを使用して安全に破棄（GPU処理完了を考慮）
        void CleanupDestroyed();

        /// @brief 全オブジェクトをクリア（シーン終了時など）
        void Clear();

        /// @brief 名前が一致する最初のオブジェクトに削除マークをつける（Undo 用）
        /// @param name 削除対象のオブジェクト名
        /// @return 見つかった場合 true
        bool DestroyByName(const std::string& name);

        /// @brief 登録されているオブジェクト数を取得
        /// @return オブジェクト数
        size_t GetObjectCount() const { return objects_.size(); }

        /// @brief 全オブジェクトのリストを取得（読み取り専用）
        /// @return オブジェクトリストの const 参照
        const std::deque<std::unique_ptr<GameObject>>& GetAllObjects() const { return objects_; }

        // ===== コンポーネント横断イテレーション =====

        /// @brief シーン内の指定型コンポーネントすべてに処理を適用する（dynamic_cast 走査の置き換え先）
        /// @tparam T  コンポーネント型（基底型でも引ける）
        /// @tparam Fn `void(T&)` または `void(T&, GameObject&)`
        /// @note 非アクティブ／削除マーク済みはスキップ。走査は線形（型別インデックスは作らない）。
        template <typename T, typename Fn>
        void ForEachComponent(Fn&& fn) {
            for (auto& obj : objects_) {
                if (!obj || !obj->IsActive() || obj->IsMarkedForDestroy()) {
                    continue;
                }
                // GetComponents<T>() は vector を確保するので使わない（毎フレームの
                // ホットパスから呼ばれるため、スロットを直接走査する）
                for (const auto& slot : obj->GetAllComponents()) {
                    if (!slot || !slot->IsEnabled()) {
                        continue;
                    }
                    if (auto* component = dynamic_cast<T*>(slot.get())) {
                        if constexpr (std::is_invocable_v<Fn, T&, GameObject&>) {
                            fn(*component, *obj);
                        } else {
                            fn(*component);
                        }
                    }
                }
            }
        }

        /// @brief シーン内で最初に見つかった指定型コンポーネントを返す
        /// @return 見つからなければ nullptr
        /// @note アクティブ状態は問わない（「シーンに存在するか」の問い合わせ用。
        ///       SceneTagComponent と組み合わせて具象型のシーン走査を置き換える）。
        template <typename T>
        T* FindFirstComponent() {
            for (auto& obj : objects_) {
                if (!obj) { continue; }
                if (auto* component = obj->GetComponent<T>()) {
                    return component;
                }
            }
            return nullptr;
        }

        // ===== ID =====

        /// @brief ID からオブジェクトを引く
        /// @return 見つからなければ nullptr（削除マーク済みでもフレーム末までは返す）
        GameObject* FindObject(ObjectId id) const;

        /// @brief 名前が一致する最初のオブジェクトを引く
        /// @return 見つからなければ nullptr（削除マーク済みは返さない）
        /// @note `UpdateAll()` の途中で作られ、まだ一覧に加わっていないオブジェクトも探す。
        GameObject* FindObjectByName(const std::string& name) const;

        /// @brief オブジェクトの ID を差し替える
        /// @return 他のオブジェクトが使っている ID なら差し替えずに false
        bool AssignObjectId(GameObject& object, ObjectId id);

        /// @brief ID から引いた結果が変わる操作（破棄・ID の差し替え・コンポーネントの付け外しと解放）のたびに進む番号
        const std::uint64_t& GetReferenceEpoch() const noexcept { return referenceEpoch_; }

        /// @brief ID から引いた結果を引き直させる（コンポーネントを付け外ししたときに呼ぶ）
        void InvalidateReferences() noexcept { ++referenceEpoch_; }

        /// @brief この管理者が生きている間だけ期限切れにならない印
        /// @note 管理者より長く残りうる参照（スクリプトのハンドルなど）が、管理者を触る前に確かめる。
        std::weak_ptr<const GameObjectManager*> GetLifetimeToken() const noexcept { return lifetimeToken_; }

        /// @brief コライダーを持つ全オブジェクトのコライダーを CollisionWorld に登録
        /// @param collisionWorld 登録先の CollisionWorld
        void RegisterAllColliders(CollisionWorld* collisionWorld);

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

        /// @brief 保存キーの連番番号管理（1 ファイル 1 オブジェクトを保つ）
        std::map<std::string, int> serializeKeyCounters_;

        /// @brief ID → オブジェクト（登録時に入れ、破棄を確定したときに外す）
        std::unordered_map<ObjectId, GameObject*> objectsById_;

        /// @brief `GetReferenceEpoch()` の実体
        std::uint64_t referenceEpoch_ = 1;

        /// @brief 保存キーが既出なら連番を足して重複を解く
        /// @note 同じ名前で作られたオブジェクト（`CreateObject("Sphere")` を 49 回など）は
        ///       そのままだと 1 ファイルへ上書きし合い、最後の 1 個しか残らない。
        void EnsureUniqueSerializeKey(GameObject& object);

        /// @brief 保存キーから ID を決めて登録する（使用中なら計算し直す）
        void RegisterObjectId(GameObject& object);

        /// @brief pendingAdd_ を objects_ へ移動する
        void FlushPendingAdds();

        /// @brief `GetLifetimeToken()` の実体（最後に宣言し、ほかのメンバより先に壊す）
        std::shared_ptr<const GameObjectManager*> lifetimeToken_ = std::make_shared<const GameObjectManager*>(this);
    };
}
