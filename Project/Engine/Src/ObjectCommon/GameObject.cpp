#include "GameObject.h"
#include "Collider/SphereCollider.h"
#include "Collider/AABBCollider.h"
#include <cstdio>

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
#include "Graphics/Texture/TextureManager.h"
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

    // ===== アクティブ =====

    void GameObject::SetActive(bool active) { isActive_ = active; }
    bool GameObject::IsActive() const { return isActive_; }

    // ===== 破棄 =====

    void GameObject::Destroy() { markedForDestroy_ = true; }
    bool GameObject::IsMarkedForDestroy() const { return markedForDestroy_; }

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

    void GameObject::SetName(const std::string& name) {
        if (serializeKey_.empty()) {
            serializeKey_ = name;
        }
    }
    const std::string& GameObject::GetName() const { return name_; }
    const std::string& GameObject::GetSerializeKey() const { return serializeKey_; }

    const char* GameObject::GetDisplayName() const {
        if (!name_.empty()) return name_.c_str();
        if (!serializeKey_.empty()) return serializeKey_.c_str();
        return GetObjectName();
    }
    const char* GameObject::GetObjectName() const { return "GameObject"; }

    bool GameObject::IsSerializeEnabled() const { return shouldSerialize_; }
    void GameObject::SetSerializeEnabled(bool enable) { shouldSerialize_ = enable; }

#ifdef USE_IMGUI
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
        ImGui::PushID(this);

        // ── 名前フィールド（アイコン付き） ───────────────────────
        {
            static D3D12_GPU_DESCRIPTOR_HANDLE sNameIconHandle{};
            static bool sNameIconLoaded = false;
            if (!sNameIconLoaded && TextureManager::GetInstance().IsInitialized()) {
                sNameIconHandle = TextureManager::GetInstance().Load("obj.png").gpuHandle;
                sNameIconLoaded = true;
            }
            if (sNameIconLoaded) {
                ImGui::AlignTextToFramePadding();
                ImGui::ImageWithBg((ImTextureID)sNameIconHandle.ptr, ImVec2(16, 16),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImVec4(0, 0, 0, 0),
                    ImVec4(0.96f, 0.65f, 0.14f, 1.0f));
                ImGui::SameLine(0.0f, 4.0f);
            }
            char nameBuf[128];
            const char* displayText = name_.empty() ? serializeKey_.c_str() : name_.c_str();
            snprintf(nameBuf, sizeof(nameBuf), "%s", displayText);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##objName", nameBuf, sizeof(nameBuf))) {
                name_ = nameBuf;
                changed = true;
            }
        }

        // ── Active トグル ────────────────────────────────────────
        bool prevActive = isActive_;
        if (UI::Widgets::ToggleSwitch("Active", &isActive_)) {
            changed = true;
            OnImGuiActiveChanged(prevActive);
        }
        UI::Separator();

        // ── タブ判定 ─────────────────────────────────────────────
        InspectorTabDef tabs[8];
        const int tabCount = GetInspectorTabs(tabs, 8);

        if (tabCount > 0) {
            // タブアイコンのロード（TextureManager がキャッシュするため毎フレーム安全）
            D3D12_GPU_DESCRIPTOR_HANDLE iconHandles[8]{};
            auto& texMgr = TextureManager::GetInstance();
            if (texMgr.IsInitialized()) {
                for (int i = 0; i < tabCount; ++i) {
                    if (tabs[i].iconPath && tabs[i].iconPath[0] != '\0') {
                        iconHandles[i] = texMgr.Load(tabs[i].iconPath).gpuHandle;
                    }
                }
            }

            constexpr float kStripW   = 28.0f;
            constexpr float kIconSize = 16.0f;
            constexpr float kBtnPad   = 4.0f;

            // ── 左側タブストリップ ───────────────────────────────
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 4.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

                UI::Scope::ChildScope tabStrip("##PropTabs", ImVec2(kStripW, 0.0f),
                    ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(2);

                for (int i = 0; i < tabCount; ++i) {
                    const bool sel = (inspectorTab_ == i);
                    const ImVec4 tint(tabs[i].tint[0], tabs[i].tint[1], tabs[i].tint[2], tabs[i].tint[3]);
                    const ImVec4 selBg(tabs[i].selectedBg[0], tabs[i].selectedBg[1], tabs[i].selectedBg[2], tabs[i].selectedBg[3]);

                    ImGui::PushStyleColor(ImGuiCol_Button, sel ? selBg : ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.18f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kBtnPad, kBtnPad));

                    char btnId[32];
                    snprintf(btnId, sizeof(btnId), "##proptab%d", i);
                    if (ImGui::ImageButton(btnId, (ImTextureID)iconHandles[i].ptr,
                        ImVec2(kIconSize, kIconSize),
                        ImVec2(0, 0), ImVec2(1, 1),
                        ImVec4(0, 0, 0, 0), tint))
                    {
                        inspectorTab_ = i;
                    }

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(3);

                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("%s", tabs[i].tooltip);
                    }
                }
            }

            ImGui::SameLine(0.0f, 2.0f);

            // ── 右コンテンツエリア ───────────────────────────────
            {
                UI::Scope::ChildScope content("##PropContent", ImVec2(0.0f, 0.0f));
                if (inspectorTab_ >= 0 && inspectorTab_ < tabCount) {
                    changed |= DrawInspectorTabContent(inspectorTab_);
                }
            }
        } else {
            // タブ未定義: フォールバック
            changed |= DrawImGuiExtended();
        }

        DrawSaveButton();
        ImGui::PopID();
        return changed;
    }

    void GameObject::DrawSaveButton() {
        if (!shouldSerialize_ || serializeKey_.empty()) return;

        UI::Separator();
        if (ImGui::Button("Save Object##save_single")) {
            if (onSaveRequested_) {
                onSaveRequested_(this);
            }
        }
        UI::SameLine();
        UI::Hint("このオブジェクトのみ保存");
    }
#endif // USE_IMGUI

}


