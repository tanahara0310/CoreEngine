#include "pch.h"
#include "GameObjectManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Collider/CollisionManager.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
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
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
                obj->Update();
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
                renderManager->AddDrawable(obj.get());
            }
        }
    }

    void GameObjectManager::CleanupDestroyed() {

        // 前フレームの削除キューをクリア（デストラクタ呼び出し）
        destroyQueue_.clear();

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                [this](auto& obj) {
                    // unique_ptrの有効性チェック
                    if (!obj) {
                        return true;
                    }

                    // 削除マークされている場合は削除キューに移動
                    if (obj->IsMarkedForDestroy()) {
                        destroyQueue_.push_back(std::move(obj));
                        return true;
                    }

                    return false;
                }),
            objects_.end()
        );
    }

    void GameObjectManager::Clear() {
        objects_.clear();
        destroyQueue_.clear();
        nameCounters_.clear();
    }

    void GameObjectManager::RegisterAllColliders(CollisionManager* collisionManager) {
        if (!collisionManager) return;

        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy() && obj->HasCollider()) {
                Collider* collider = obj->GetCollider();
                if (collider && collider->IsEnabled()) {
                    collisionManager->RegisterCollider(collider);
                }
            }
        }
    }

    bool GameObjectManager::DestroyByName(const std::string& name)
    {
        for (auto& obj : objects_) {
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
