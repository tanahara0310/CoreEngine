#include "pch.h"
#include "GameObjectManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Collision/CollisionWorld.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif


namespace CoreEngine
{
    GameObject* GameObjectManager::SpawnRaw(std::unique_ptr<GameObject> obj) {
        if (!obj) return nullptr;

        GameObject* ptr = obj.get();

        // spawner_ を注入（このオブジェクトから Spawn<T>() が呼べるようになる）
        ptr->spawner_ = this;

        // 名前未設定の場合は GetObjectName() + 連番番号で自動付与
        if (ptr->GetName().empty()) {
            std::string baseName = ptr->GetObjectName();
            int idx = nameCounters_[baseName]++;
            ptr->SetName(baseName + "_" + std::to_string(idx));
        }

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

    void GameObjectManager::UpdateAll() {
        isUpdating_ = true;
        // アクティブで削除マークされておらず、自動更新が有効なオブジェクトのみ更新
        // 削除はCleanupDestroyed()で行われるため、直接ループで問題ない
        //
        // コンポーネントの呼び出し順（IComponent.h の契約）:
        //   [パス1] 全オブジェクト: Start() → Update() → GameObject::Update()（従来の派生クラス処理）
        //   [パス2] 全オブジェクト: LateUpdate()
        //
        // Update() を派生クラスより前に置くのは、従来 OnUpdate() が
        // TransferMatrix() の結果を上書きする前提で書かれているコードを壊さないため。
        //
        // **LateUpdate を別パスにしているのが重要**。1 オブジェクトずつ
        // 「Update → LateUpdate」と回すと、LateUpdate から他のオブジェクトを参照したとき
        // 相手がまだ Update されていない可能性が残り、**生成順への依存が消えない**。
        // 実例: 武器のジョイント追従（SkeletonSocketComponent）は追従元キャラクターの
        // アニメーション更新が終わっている必要があり、以前は
        // 「キャラクターより後に生成すること」というコメントで人間が担保していた。
        // 全 Update 完了後に LateUpdate をまとめて回せば、生成順に関係なく必ず
        // 最新の姿勢を読める。
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                obj->DispatchComponentStart();
                obj->DispatchComponentUpdate();
                obj->Update();
            }
        }
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                obj->DispatchComponentLateUpdate();
            }
        }
        isUpdating_ = false;

        // Update中に Spawn<T>() されたオブジェクトをまとめて追加
        FlushPendingAdds();
    }

    void GameObjectManager::FlushPendingAdds() {
        for (auto& obj : pendingAdd_) {
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
        for (auto& obj : objects_) {
            if (obj) {
                obj->ReleaseRetiredColliders();
                obj->ReleaseRetiredComponents();
            }
        }

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                [this](auto& obj) {
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
                        destroyQueue_.push_back(std::move(obj));
                        return true;
                    }

                    return false;
                }),
            objects_.end()
        );
    }

    void GameObjectManager::Clear() {
        // シーン遷移時。生きているオブジェクトの OnDestroy() を先に発行してから捨てる
        // （CollisionFeature::Finalize と同じ理由で、後始末の機会を与える）。
        for (auto& obj : objects_) {
            if (obj) obj->DispatchComponentDestroy();
        }
        objects_.clear();
        destroyQueue_.clear();
        nameCounters_.clear();
    }

    void GameObjectManager::RegisterAllColliders(CollisionWorld* collisionWorld) {
        if (!collisionWorld) return;

        // ColliderComponent を持つオブジェクトだけを走る。
        // 以前は全オブジェクトの固定メンバ colliders_ を無条件に舐めていたので、
        // コライダーを 1 本も持たないオブジェクト（スカイボックス・UI・デバッグ線）も
        // 毎フレーム空振りしていた。
        //
        // 非アクティブ／削除マーク済みのスキップは ForEachComponent が行う（従来と同条件）。
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

#ifdef USE_IMGUI
    void GameObjectManager::DrawSingleObjectImGui(GameObject* obj)
    {
        if (!obj) {
            UI::Hint("オブジェクトを選択してください");
            return;
        }

        if (editCommitCallback_) {
            obj->SetEditCommitCallback(editCommitCallback_);
        }
        if (onSaveRequestCallback_) {
            obj->SetSaveRequestCallback(onSaveRequestCallback_);
        }

        if (obj->IsMarkedForDestroy()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }

        bool changed = obj->DrawImGui();

        if (obj->IsMarkedForDestroy()) {
            ImGui::PopStyleColor();
        }

        if (changed && onChangedCallback_) {
            onChangedCallback_(obj);
        }
    }
#endif // USE_IMGUI
}
