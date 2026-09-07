#include "pch.h"
#include "GameObject.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include <cstdio>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
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

    void GameObject::Draw(const Camera* camera) {
        (void)camera;
    }

    void GameObject::Draw(const DrawViewInfo& view) {
        // 描画はコンポーネントが答える（継承で Draw を override する必要はない）。
        // 1 つも無ければ旧経路（Draw(カメラ)）へフォールバックする。
        bool rendered = false;
        for (const auto& slot : GetAllComponents()) {
            if (!slot || !slot->IsEnabled()) { continue; }
            if (auto* renderable = dynamic_cast<IRenderableComponent*>(slot.get())) {
                renderable->Render(view);
                rendered = true;
            }
        }
        if (!rendered) {
            Draw(view.GetCamera());
        }
    }

    // ===== トランスフォーム（コンポーネントへの委譲） =====

    Vector3 GameObject::GetWorldPosition() const {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->GetWorldPosition();   // 親の階層を含むワールド位置
        }
        if (auto* source = GetComponent<ITransformSource>()) {
            return const_cast<ITransformSource*>(source)->Translate();
        }
        return {};
    }

    Vector3 GameObject::GetWorldScale() const {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->GetWorldScale();      // 親の階層スケールを含む
        }
        if (auto* source = GetComponent<ITransformSource>()) {
            return const_cast<ITransformSource*>(source)->Scale();
        }
        return { 1.0f, 1.0f, 1.0f };
    }

    bool GameObject::TryApplyCollisionPush(const Vector3& delta) {
        if (auto* transform = GetComponent<TransformComponent>()) {
            return transform->ApplyWorldDelta(delta);
        }
        return false;
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

    // 描画パス・ブレンドは描画コンポーネントが答える（無ければ既定値）

    RenderPassType GameObject::GetRenderPassType() const {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            return renderable->GetRenderPassType();
        }
        return RenderPassType::Model;
    }

    BlendMode GameObject::GetBlendMode() const {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            return renderable->GetBlendMode();
        }
        return BlendMode::kBlendModeNone;
    }

    void GameObject::SetBlendMode(BlendMode blendMode) {
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            renderable->SetBlendMode(blendMode);
        }
    }

    RenderItem GameObject::BuildRenderItem() const {
        RenderItem item;
        item.object = const_cast<GameObject*>(this);
        item.passType = GetRenderPassType();
        item.blendMode = GetBlendMode();
        item.renderOrderOverride = GetRenderOrder();

        // 描画コンポーネントが種別を明示していればそれを尊重する
        if (auto* renderable = GetComponent<IRenderableComponent>()) {
            item.kind = renderable->GetRenderItemKind();
        }

        if (item.kind == RenderItemKind::Default) {
            if (item.passType == RenderPassType::SkyBox) {
                item.kind = RenderItemKind::SkyBox;
            } else if (item.passType == RenderPassType::WaterSurface) {
                item.kind = RenderItemKind::WaterSurface;
            } else if (item.blendMode != BlendMode::kBlendModeNone) {
                item.kind = RenderItemKind::Transparent;
            }
        }

        return item;
    }

    // ===== 衝突イベント =====

    void GameObject::OnCollisionEnter(GameObject* other) { (void)other; }
    void GameObject::OnCollisionStay(GameObject* other) { (void)other; }
    void GameObject::OnCollisionExit(GameObject* other) { (void)other; }

    // 接触情報つき版の既定実装は ①ColliderComponent の購読者へ配り ②旧 API へ転送する。
    // これでコンポーネント購読（継承不要）と GameObject* 版の override が同時に動く。
    void GameObject::OnCollisionEnter(const CollisionInfo& info) {
        if (auto* colliders = TryGetColliders()) { colliders->DispatchEnter(info); }
        OnCollisionEnter(info.other);
    }
    void GameObject::OnCollisionStay(const CollisionInfo& info) {
        if (auto* colliders = TryGetColliders()) { colliders->DispatchStay(info); }
        OnCollisionStay(info.other);
    }
    void GameObject::OnCollisionExit(const CollisionInfo& info) {
        if (auto* colliders = TryGetColliders()) { colliders->DispatchExit(info); }
        OnCollisionExit(info.other);
    }

    // ===== コライダー =====

    // 問い合わせ系は TryGetColliders()（生成しない）を使う。GetColliders() を使うと
    // 「持っているか調べただけ」で空のコライダー集合が生えてしまう。

    bool GameObject::HasCollider() const {
        const ColliderComponent* colliders = TryGetColliders();
        return colliders && !colliders->IsEmpty();
    }

    Collider* GameObject::GetCollider() {
        ColliderComponent* colliders = TryGetColliders();
        return colliders ? colliders->GetFirst() : nullptr;
    }

    const Collider* GameObject::GetCollider() const {
        const ColliderComponent* colliders = TryGetColliders();
        return colliders ? colliders->GetFirst() : nullptr;
    }

    void GameObject::RemoveCollider() {
        if (ColliderComponent* colliders = TryGetColliders()) {
            colliders->RemoveAll();
        }
    }

    void GameObject::ReleaseRetiredColliders() {
        if (ColliderComponent* colliders = TryGetColliders()) {
            colliders->ReleaseRetired();
        }
    }

    // 追加系はオンデマンド生成でよい（呼んだ時点でコライダーが要ると確定している）

    Collider& GameObject::AddSphereCollider(float radius, CollisionLayer layer) {
        return GetColliders().AddSphere(radius, layer);
    }

    Collider& GameObject::AddAABBCollider(const Vector3& size, CollisionLayer layer) {
        return GetColliders().AddBox(size, layer);
    }

    // ===== 名前 / シリアライズ =====

    void GameObject::SetName(const std::string& name) {
        name_ = name;
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

    void GameObject::NotifyEditCommitted(const Vector3& beforeTranslate, const Vector3& beforeRotate,
        const Vector3& beforeScale, bool beforeActive) {
        if (onEditCommitted_) {
            onEditCommitted_(this, beforeTranslate, beforeRotate, beforeScale, beforeActive);
        }
    }

    int GameObject::BuildComponentTabs(InspectorTabDef* outTabs, int maxTabs) const {
        if (!outTabs || maxTabs <= 0) { return 0; }

        int count = 0;
        for (const auto& component : GetAllComponents()) {
            if (!component) { continue; }
            if (count >= maxTabs) { break; }

            InspectorTabDef& tab = outTabs[count];
            tab.iconPath = component->GetInspectorIcon();
            tab.tooltip = component->GetInspectorName();
            component->GetInspectorIconColor(tab.tint);

            // 選択中の背景はアイコン色を薄くしたもの（レガシータブと同じ見た目にする）
            tab.selectedBg[0] = tab.tint[0];
            tab.selectedBg[1] = tab.tint[1];
            tab.selectedBg[2] = tab.tint[2];
            tab.selectedBg[3] = 0.25f;

            ++count;
        }
        return count;
    }

    bool GameObject::DrawComponentTabContent(int tabIndex) {
        const auto& components = GetAllComponents();

        // BuildComponentTabs と同じ順序で走査する（nullptr は両方で飛ばす）
        int index = 0;
        for (const auto& component : components) {
            if (!component) { continue; }

            if (index == tabIndex) {
                bool changed = false;

                bool enabled = component->IsEnabled();
                if (ImGui::Checkbox("有効", &enabled)) {
                    component->SetEnabled(enabled);
                    changed = true;
                }
                UI::Separator();

                // DrawInspector() の戻り値は「値が変わったか」であって
                // 「何か描いたか」ではない。中身の有無はカーソルが進んだかで見る
                const float cursorBefore = ImGui::GetCursorPosY();
                changed |= component->DrawInspector();

                if (ImGui::GetCursorPosY() <= cursorBefore) {
                    UI::Hint("このコンポーネントに編集項目はありません");
                }
                return changed;
            }
            ++index;
        }
        return false;
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
        const int objectTabCount = GetInspectorTabs(tabs, 8);
        const int componentTabCount = objectTabCount < 8
            ? BuildComponentTabs(tabs + objectTabCount, 8 - objectTabCount)
            : 0;
        const int tabCount = objectTabCount + componentTabCount;

        // 前回選んでいたタブがコンポーネントの増減で範囲外になることがある
        if (inspectorTab_ >= tabCount) { inspectorTab_ = 0; }

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
                    changed |= inspectorTab_ < objectTabCount
                        ? DrawInspectorTabContent(inspectorTab_)
                        : DrawComponentTabContent(inspectorTab_ - objectTabCount);
                }
            }
        } else {
            // タブもコンポーネントも無い: フォールバック
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


