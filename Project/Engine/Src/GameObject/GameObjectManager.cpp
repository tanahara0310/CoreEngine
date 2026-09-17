#include "pch.h"
#include "GameObjectManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Collision/CollisionWorld.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>


namespace CoreEngine
{
    GameObject* GameObjectManager::SpawnRaw(std::unique_ptr<GameObject> obj) {
        if (!obj) return nullptr;

        GameObject* ptr = obj.get();

        // spawner_ を注入（このオブジェクトから Spawn<T>() が呼べるようになる）
        ptr->spawner_ = this;
        ptr->objectManager_ = this;

        // 名前未設定の場合は GetObjectName() + 連番番号で自動付与
        if (ptr->GetName().empty()) {
            std::string baseName = ptr->GetObjectName();
            int idx = nameCounters_[baseName]++;
            ptr->SetName(baseName + "_" + std::to_string(idx));
        }

        // 保存キーを 1 シーンで一意にする（名前は重複してよい）
        EnsureUniqueSerializeKey(*ptr);

        // 保存キーから ID を決めて登録する
        RegisterObjectId(*ptr);

        // オブジェクト固有の初期化を自動実行
        ptr->Initialize();

        // Update中は pending に積む（deque への push_back は全イテレータを無効化するため）
        if (isUpdating_) {
            pendingAdd_.push_back(std::move(obj));
        } else {
            objects_.push_back(std::move(obj));
        }

        return ptr;
    }

    void GameObjectManager::EnsureUniqueSerializeKey(GameObject& object)
    {
        const std::string base = object.GetSerializeKey();
        if (base.empty()) {
            return;
        }

        // 初出はそのまま。既存の保存ファイルとの対応を切らないため
        auto [entry, inserted] = serializeKeyCounters_.try_emplace(base, 0);
        if (inserted) {
            return;
        }

        std::string candidate;
        do {
            candidate = base + "_" + std::to_string(++entry->second);
        } while (serializeKeyCounters_.find(candidate) != serializeKeyCounters_.end());

        serializeKeyCounters_.emplace(candidate, 0);
        object.SetSerializeKey(candidate);
    }

    void GameObjectManager::RegisterObjectId(GameObject& object)
    {
        const std::string& key = object.GetSerializeKey();
        const ObjectId baseId = ObjectId::FromKey(key);

        ObjectId id = baseId;
        for (int salt = 1; objectsById_.contains(id); ++salt) {
            id = ObjectId::FromKey(key + "#" + std::to_string(salt));
        }
        if (!(id == baseId)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "GameObjectManager: \"{}\" の ID {} は使用中なので {} にしました",
                key, baseId.ToString(), id.ToString());
        }

        object.objectId_ = id;
        objectsById_.emplace(id, &object);
    }

    GameObject* GameObjectManager::FindObject(ObjectId id) const
    {
        const auto it = objectsById_.find(id);
        return it != objectsById_.end() ? it->second : nullptr;
    }

    GameObject* GameObjectManager::FindObjectByName(const std::string& name) const
    {
        for (const auto& obj : objects_) {
            if (obj && !obj->IsMarkedForDestroy() && obj->GetName() == name) {
                return obj.get();
            }
        }
        // UpdateAll() の途中で作られ、まだ objects_ へ移していないもの
        for (const auto& obj : pendingAdd_) {
            if (obj && !obj->IsMarkedForDestroy() && obj->GetName() == name) {
                return obj.get();
            }
        }
        return nullptr;
    }

    bool GameObjectManager::AssignObjectId(GameObject& object, ObjectId id)
    {
        if (!id.IsValid()) {
            return false;
        }
        if (object.objectId_ == id) {
            return true;
        }
        if (const auto used = objectsById_.find(id);
            used != objectsById_.end() && used->second != &object) {
            return false;
        }

        if (const auto current = objectsById_.find(object.objectId_);
            current != objectsById_.end() && current->second == &object) {
            objectsById_.erase(current);
        }
        object.objectId_ = id;
        objectsById_[id] = &object;
        ++referenceEpoch_;
        return true;
    }

    void GameObjectManager::UpdateAll(const std::function<void()>& afterUpdatePass) {
        isUpdating_ = true;
        // アクティブかつ削除マークが無く、自動更新が有効なオブジェクトのみ更新する。
        // 呼び出し順は [パス1] Start → コンポーネント Update → GameObject::Update を全員分、
        // afterUpdatePass とワールド行列の転送、[パス2] LateUpdate を全員分。別パスにすることで、
        // 他オブジェクトを参照する処理（ジョイント追従など）が生成順に依存しなくなる。
        // 転送はパス1 で書き換えた座標をこのフレームの描画と当たり判定に出すためのもので、
        // パス2 より前に置くので、LateUpdate でワールド行列を上書きする処理（ソケット追従など）は残る
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                obj->DispatchComponentStart();
                obj->DispatchComponentUpdate();
                obj->Update();
            }
        }
        if (afterUpdatePass) {
            afterUpdatePass();
        }
        SyncTransforms();
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                obj->DispatchComponentLateUpdate();
            }
        }
        isUpdating_ = false;

        // Update中に Spawn<T>() されたオブジェクトをまとめて追加
        FlushPendingAdds();
    }

    void GameObjectManager::SyncTransforms() {
        // 親を先に転送しないと子が古い親行列で合成されるが、走査順は UpdateAll() と
        // 同じ登録順なので、再生中と停止中で見え方が変わることはない
        ForEachComponent<TransformComponent>([](TransformComponent& transform) {
            transform.SyncWorldMatrix();
            });
    }

    void GameObjectManager::FlushPendingAdds() {
        for (auto& obj : pendingAdd_) {
            // 作った直後に書いた座標を、次の TransformComponent::Update を待たずに描画へ出す
            if (TransformComponent* const transform = obj ? obj->GetComponent<TransformComponent>() : nullptr) {
                transform->SyncWorldMatrix();
            }
            objects_.push_back(std::move(obj));
        }
        pendingAdd_.clear();
    }

    void GameObjectManager::RegisterAllToRender(RenderManager* renderManager) {
        if (!renderManager) return;

        // アクティブかつ表示状態で削除マークされていないオブジェクトのみ登録
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                renderManager->AddRenderItem(obj->BuildRenderItem());
            }
        }
    }

    void GameObjectManager::CleanupDestroyed() {

        // 前フレームの削除キューをクリア（デストラクタ呼び出し）
        destroyQueue_.clear();

        // 取り外し済みコライダー／コンポーネントの実体を解放する。
        // 衝突判定（PostObjectUpdate）より後のこのタイミングでしか解放してはいけない
        // ——判定ループが colliders_ に生ポインタを保持しているため。
        bool referencesChanged = false;
        for (auto& obj : objects_) {
            if (obj) {
                obj->ReleaseRetiredColliders();
                if (obj->ReleaseRetiredComponents()) {
                    referencesChanged = true;
                }
            }
        }

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                [this, &referencesChanged](auto& obj) {
                    // unique_ptrの有効性チェック
                    if (!obj) {
                        return true;
                    }

                    // 削除マークされている場合は削除キューに移動
                    if (obj->IsMarkedForDestroy()) {
                        // 実体の解放は次フレームの destroyQueue_.clear() まで遅延するが、
                        // OnDestroy() は「もう死んだ」と分かった今フレームで発行する
                        // （他コンポーネントがまだ生きているうちに後始末できる）。
                        obj->DispatchComponentDestroy();

                        // ID から引けないようにする
                        if (const auto it = objectsById_.find(obj->GetObjectId());
                            it != objectsById_.end() && it->second == obj.get()) {
                            objectsById_.erase(it);
                        }
                        referencesChanged = true;

                        destroyQueue_.push_back(std::move(obj));
                        return true;
                    }

                    return false;
                }),
            objects_.end()
        );

        // ObjectRef が控えている実体を引き直させる
        if (referencesChanged) {
            ++referenceEpoch_;
        }
    }

    void GameObjectManager::Clear() {
        // シーン遷移時。生きているオブジェクトの OnDestroy() を先に発行してから捨てる
        // （CollisionFeature::Finalize と同じ理由で、後始末の機会を与える）。
        for (auto& obj : objects_) {
            if (obj) obj->DispatchComponentDestroy();
        }

        // 捨てる前に ID から引けないようにする
        objectsById_.clear();
        ++referenceEpoch_;

        objects_.clear();
        destroyQueue_.clear();
        nameCounters_.clear();
    }

    void GameObjectManager::RegisterAllColliders(CollisionWorld* collisionWorld) {
        if (!collisionWorld) return;

        // ColliderComponent を持つオブジェクトだけを走る
        // （全オブジェクトを舐めると、コライダーを持たない大半が毎フレーム空振りする）。
        // 非アクティブ／削除マーク済みのスキップは ForEachComponent が行う。
        ForEachComponent<ColliderComponent>(
            [collisionWorld](ColliderComponent& colliders) {
                // 1 オブジェクトが複数のコライダーを持てる（本体判定 + 攻撃判定など）
                colliders.ForEachEnabled([collisionWorld](Collider& collider) {
                    collisionWorld->RegisterCollider(&collider);
                    });
            });
    }

    bool GameObjectManager::DestroyByName(const std::string& name)
    {
        for (auto it = objects_.rbegin(); it != objects_.rend(); ++it) {
            auto& obj = *it;
            if (obj && obj->GetName() == name && !obj->IsMarkedForDestroy()) {
                obj->Destroy();
                return true;
            }
        }
        return false;
    }

}
