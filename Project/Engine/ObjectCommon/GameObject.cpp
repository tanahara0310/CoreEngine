#include "GameObject.h"
#include "Engine/Graphics/Model/Model.h"
#include <cstdio>

#ifdef _DEBUG
#include <imgui.h>
#endif // _DEBUG



namespace CoreEngine
{
    namespace {
        EngineSystem* sEngine = nullptr;
    }

    void GameObject::Initialize(EngineSystem* engine) {
        if (sEngine == nullptr) {
            sEngine = engine;
        }
    }

    EngineSystem* GameObject::GetEngineSystem() const {
        return sEngine;
    }

#ifdef _DEBUG
    bool GameObject::DrawImGui() {
        bool changed = false;

        // オブジェクト名とアドレスを組み合わせた一意のヘッダー
        // 設定された名前がある場合はそれを使用、なければクラス名を使用
        const char* displayName = name_.empty() ? GetObjectName() : name_.c_str();
        char headerLabel[256];
        snprintf(headerLabel, sizeof(headerLabel), "%s##%p", displayName, (void*)this);

        if (ImGui::CollapsingHeader(headerLabel)) {
            ImGui::PushID(this);

            // アクティブ状態
            bool active = isActive_;
            if (ImGui::Checkbox("Active", &active)) {
                isActive_ = active;
                changed = true;
            }

            // 自動更新フラグ
            bool autoUpdate = autoUpdate_;
            if (ImGui::Checkbox("Auto Update", &autoUpdate)) {
                autoUpdate_ = autoUpdate;
                changed = true;
            }

            ImGui::Separator();

            // トランスフォーム
            if (ImGui::TreeNode("Transform")) {
                Vector3& pos = transform_.translate;
                Vector3& rot = transform_.rotate;
                Vector3& scale = transform_.scale;

                changed |= ImGui::DragFloat3("Position", &pos.x, 0.1f);
                changed |= ImGui::DragFloat3("Rotation", &rot.x, 0.01f);
                changed |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);

                ImGui::TreePop();
            }

            // 派生クラスの拡張UI
            changed |= DrawImGuiExtended();

            ImGui::PopID();
        }

        return changed;
    }

#endif // _DEBUG

    void GameObject::DrawShadow(ID3D12GraphicsCommandList* cmdList) {
        // モデルがある場合のみシャドウを描画
        if (model_ && model_->IsInitialized()) {
            model_->DrawShadow(transform_, cmdList);
        }
    }

    Collider& GameObject::AddSphereCollider(float radius, CollisionLayer layer) {
        collider_ = std::make_unique<SphereCollider>(this, radius);
        collider_->SetLayer(layer);
        return *collider_;
    }

    Collider& GameObject::AddAABBCollider(const Vector3& size, CollisionLayer layer) {
        collider_ = std::make_unique<AABBCollider>(this, size);
        collider_->SetLayer(layer);
        return *collider_;
    }
}
