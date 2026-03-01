#include "GameObjectManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/Collider/CollisionManager.h"
#include <algorithm>

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
    void GameObjectManager::UpdateAll() {
        // アクティブで削除マークされておらず、自動更新が有効なオブジェクトのみ更新
        // 削除はCleanupDestroyed()で行われるため、直接ループで問題ない
        for (auto& obj : objects_) {
            if (obj && obj->IsActive() && !obj->IsMarkedForDestroy() && obj->IsAutoUpdate()) {
                obj->Update();
            }
        }
    }

    void GameObjectManager::RegisterAllToRender(RenderManager* renderManager) {
        if (!renderManager) return;

        // アクティブで削除マークされていないオブジェクトのみ登録
        // 事前フィルタリング済みのため、RenderManager側では簡易チェックのみ
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

#ifdef _DEBUG
    void GameObjectManager::DrawAllImGui() {
        ImGui::Text("Total Objects: %zu", objects_.size());
        ImGui::Text("Destroy Queue: %zu", destroyQueue_.size());
        ImGui::Separator();

        for (auto& obj : objects_) {
            if (obj) {
                // Undo/Redo 用コールバックを毎フレーム設定
                if (editCommitCallback_) {
                    obj->SetEditCommitCallback(editCommitCallback_);
                }

                if (obj->IsMarkedForDestroy()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }

                bool changed = obj->DrawImGui();

                if (obj->IsMarkedForDestroy()) {
                    ImGui::PopStyleColor();
                }

                if (changed && onChangedCallback_) {
                    onChangedCallback_(obj.get());
                }
            }
        }
    }
#endif
}
