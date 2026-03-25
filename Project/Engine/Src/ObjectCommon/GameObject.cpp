#include "GameObject.h"
#include "Collider/SphereCollider.h"
#include "Collider/AABBCollider.h"
#include <cstdio>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace CoreEngine
{
    namespace {
        EngineSystem* sEngine = nullptr;
    }

    // ===== ライフサイクル =====

    void GameObject::SetEngine(EngineSystem* engine) {
        if (sEngine == nullptr) {
            sEngine = engine;
        }
    }

    EngineSystem* GameObject::GetEngineSystem() const {
        return sEngine;
    }

    void GameObject::Update() {}

    void GameObject::Draw(const ICamera* camera) {
        (void)camera;
    }

    void GameObject::DrawShadow(ID3D12GraphicsCommandList* cmdList) {
        (void)cmdList;
    }

    // ===== アクティブ / 表示 =====

    void GameObject::SetActive(bool active) { isActive_ = active; }
    bool GameObject::IsActive() const { return isActive_; }

    void GameObject::SetVisible(bool visible) { isVisible_ = visible; }
    bool GameObject::IsVisible() const { return isVisible_; }

    // ===== 破棄 =====

    void GameObject::Destroy() { markedForDestroy_ = true; }
    bool GameObject::IsMarkedForDestroy() const { return markedForDestroy_; }

    // ===== 更新制御 =====

    void GameObject::SetAutoUpdate(bool autoUpdate) { autoUpdate_ = autoUpdate; }
    bool GameObject::IsAutoUpdate() const { return autoUpdate_; }

    // ===== 描画制御 =====

    void GameObject::SetRenderOrder(int order) { renderOrder_ = order; }
    std::optional<int> GameObject::GetRenderOrder() const { return renderOrder_; }
    void GameObject::ResetRenderOrder() { renderOrder_ = std::nullopt; }

    RenderPassType GameObject::GetRenderPassType() const { return RenderPassType::Model; }
    BlendMode GameObject::GetBlendMode() const { return BlendMode::kBlendModeNone; }

    void GameObject::SetBlendMode(BlendMode blendMode) {
        (void)blendMode;
    }

    // ===== 衝突イベント =====

    void GameObject::OnCollisionEnter(GameObject* other) { (void)other; }
    void GameObject::OnCollisionStay(GameObject* other) { (void)other; }
    void GameObject::OnCollisionExit(GameObject* other) { (void)other; }

    Vector3 GameObject::GetWorldPosition() const { return {}; }

    // ===== コライダー =====

    bool GameObject::HasCollider() const { return collider_ != nullptr; }
    Collider* GameObject::GetCollider() { return collider_.get(); }
    const Collider* GameObject::GetCollider() const { return collider_.get(); }
    void GameObject::RemoveCollider() { collider_.reset(); }

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

    // ===== 名前 / シリアライズ =====

    void GameObject::SetName(const std::string& name) { name_ = name; }
    const std::string& GameObject::GetName() const { return name_; }
    const char* GameObject::GetObjectName() const { return "GameObject"; }

    bool GameObject::IsSerializeEnabled() const { return shouldSerialize_; }
    void GameObject::SetSerializeEnabled(bool enable) { shouldSerialize_ = enable; }

#ifdef _DEBUG
    // ===== デバッグ UI =====

    bool GameObject::DrawImGuiExtended() { return false; }

    void GameObject::SetEditCommitCallback(EditCommitCallback cb) {
        onEditCommitted_ = std::move(cb);
    }

    void GameObject::SetSaveRequestCallback(SaveRequestCallback cb) {
        onSaveRequested_ = std::move(cb);
    }

    bool GameObject::DrawImGui() {
        bool changed = false;

        const char* displayName = name_.empty() ? GetObjectName() : name_.c_str();
        char headerLabel[256];
        snprintf(headerLabel, sizeof(headerLabel), "%s##%p", displayName, (void*)this);

        if (ImGui::CollapsingHeader(headerLabel)) {
            ImGui::PushID(this);

            bool prevActive = isActive_;
            if (ImGui::Checkbox("Active", &isActive_)) {
                changed = true;
                OnImGuiActiveChanged(prevActive);
            }
            if (ImGui::Checkbox("Visible", &isVisible_)) { changed = true; }
            bool autoUpdate = autoUpdate_;
            if (ImGui::Checkbox("Auto Update", &autoUpdate)) {
                autoUpdate_ = autoUpdate;
                changed = true;
            }

            ImGui::Separator();
            changed |= DrawImGuiExtended();
            DrawSaveButton();
            ImGui::PopID();
        }

        return changed;
    }

    void GameObject::DrawSaveButton() {
        if (!shouldSerialize_ || name_.empty()) return;

        ImGui::Separator();
        if (ImGui::Button("保存##save_single")) {
            if (onSaveRequested_) {
                onSaveRequested_(this);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("このオブジェクトのみ");
    }
#endif

}


