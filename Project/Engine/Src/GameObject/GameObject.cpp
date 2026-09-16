#include "pch.h"
#include "GameObject.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include <cstdio>

#ifdef USE_IMGUI
#include "Editor/External/ExternalCodeEditor.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Scene/ComponentEditing.h"
#include "Editor/Scene/PrefabEditing.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/PrefabSystem.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
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

    json GameObject::Serialize() const
    {
        // 派生固有の値を先に受け取り、共通部分を被せる。
        // 共通部分を派生が書き換えられないようにするため、この順で足す
        json j = OnSerialize();
        if (!j.is_object()) {
            j = json::object();
        }

        j["active"] = IsActive();
        if (!name_.empty()) {
            j["name"] = name_;
        }

        json components = SerializeComponents();
        if (!components.empty()) {
            j["components"] = std::move(components);
        }
        return j;
    }

    void GameObject::Deserialize(const json& j)
    {
        if (!j.is_object()) {
            return;
        }

        if (j.contains("active") && j["active"].is_boolean()) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("name") && j["name"].is_string()) {
            name_ = j["name"].get<std::string>();
        }
        if (j.contains("components")) {
            DeserializeComponents(j["components"]);
        }

        // 派生固有の値は後。コンポーネントから作る派生データを上書きできるようにする
        OnDeserialize(j);
    }


#ifdef USE_IMGUI
    // ===== デバッグ UI =====

    namespace
    {
        namespace Theme = Editor::Theme;

        /// オブジェクトの ⋮ のメニュー
        constexpr const char* kObjectMenuId = "##objectMenu";

        /// プレハブの行の右クリックのメニュー
        constexpr const char* kPrefabMenuId = "##prefabMenu";

        /// コンポーネントの ⋮ のメニュー
        constexpr const char* kComponentMenuId = "##componentMenu";

        /// スクリプトのセクションの末尾のボタン
        constexpr const char* kOpenScriptLabel = "◇ スクリプトを開く";

        /// @brief パスからフォルダと拡張子を除いた名前（UTF-8 のまま扱う）
        std::string StemOf(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("/\\");
            std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
            const std::size_t dot = name.find_last_of('.');
            if (dot != std::string::npos && dot > 0) {
                name.resize(dot);
            }
            return name;
        }
    }

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

    bool GameObject::DrawInspectorHeader() {
        bool changed = false;
        const ImGuiStyle& style = ImGui::GetStyle();

        // 種類の記号（プレハブから作ったものは ◈）
        ImGui::AlignTextToFramePadding();
        if (IsPrefabInstance()) {
            ImGui::TextColored(Theme::kAccentHover, "◈");
        } else {
            ImGui::TextColored(Theme::kTextDim, "◆");
        }

        // 有効
        ImGui::SameLine();
        const bool prevActive = isActive_;
        if (ImGui::Checkbox("##active", &isActive_)) {
            changed = true;
            OnImGuiActiveChanged(prevActive);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", isActive_ ? "有効（外すと更新も描画もしない）" : "無効（入れると動く）");
        }

        // 名前（右端に ⋮ の場所を残す）
        const float menuWidth = ImGui::CalcTextSize("⋮").x + style.FramePadding.x * 2.0f;
        ImGui::SameLine();
        ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - menuWidth - style.ItemSpacing.x));
        char nameBuf[128];
        const char* displayText = name_.empty() ? serializeKey_.c_str() : name_.c_str();
        snprintf(nameBuf, sizeof(nameBuf), "%s", displayText);
        if (ImGui::InputText("##objName", nameBuf, sizeof(nameBuf))) {
            name_ = nameBuf;
            changed = true;
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kTransparent);
        if (ImGui::Button("⋮##objectMenuButton", ImVec2(menuWidth, 0.0f))) {
            ImGui::OpenPopup(kObjectMenuId);
        }
        ImGui::PopStyleColor();
        if (ImGui::BeginPopup(kObjectMenuId)) {
            const bool canSave = shouldSerialize_ && !serializeKey_.empty() && onSaveRequested_;
            if (ImGui::MenuItem("このオブジェクトだけ保存", nullptr, false, canSave)) {
                onSaveRequested_(this);
            }
            ImGui::EndPopup();
        }

        // プレハブから作ったものは、元のプレハブを参照の欄で出す
        if (IsPrefabInstance()) {
            const std::string path = GetPrefab().GetPath();
            const bool labelHovered = InspectorLayout::BeginRow("Prefab", Theme::kTextDim, kPrefabMenuId);
            const bool fieldHovered = InspectorLayout::ReferenceField(
                InspectorLayout::AssetGlyph(AssetType::Prefab), StemOf(path).c_str(),
                InspectorLayout::ShortId(GetPrefab().GetGuid()).c_str());
            ImGui::OpenPopupOnItemClick(kPrefabMenuId, ImGuiPopupFlags_MouseButtonRight);
            if (labelHovered || fieldHovered) {
                ImGui::SetTooltip("%s\n右クリックでプレハブの操作", path.c_str());
            }
            if (ImGui::BeginPopup(kPrefabMenuId)) {
                ImGui::TextDisabled("%s", path.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("プレハブへ適用", nullptr, false, objectManager_ != nullptr)) {
                    PrefabEditing::ApplyObject(*objectManager_, *this);
                }
                if (ImGui::MenuItem("プレハブとのつながりを外す")) {
                    PrefabEditing::Unlink(*this);
                }
                ImGui::EndPopup();
            }
        }
        return changed;
    }

    bool GameObject::DrawComponentSection(IComponent& component, IComponent*& removeRequest) {
        bool changed = false;
        ImGui::PushID(&component);

        ComponentFactory& factory = ComponentFactory::Get();
        const std::string typeName = component.GetTypeName();
        const bool isScript = factory.IsRuntimeType(typeName);
        const std::filesystem::path sourceFile = isScript ? factory.GetSourceFile(typeName) : std::filesystem::path{};
        const std::string tag = sourceFile.empty() ? std::string("AS")
            : Logger::GetInstance().PathToUtf8(sourceFile.filename());

        // 見出し
        bool enabled = component.IsEnabled();
        bool enabledChanged = false;
        InspectorLayout::SectionHeader header;
        header.name = component.GetInspectorName();
        header.origin = isScript ? InspectorLayout::Origin::Script : InspectorLayout::Origin::Native;
        header.enabled = &enabled;
        header.tag = isScript ? tag.c_str() : nullptr;
        header.menuId = kComponentMenuId;
        const bool open = InspectorLayout::DrawSectionHeader(header, enabledChanged);
        if (enabledChanged) {
            component.SetEnabled(enabled);
            changed = true;
        }

        // 記述子を持つ型は、中身を描くのと既定値へ戻すのに同じ文脈を使う
        const Reflection::TypeDescriptor* descriptor = component.GetTypeDescriptor();
        const bool reflected = descriptor && InspectorRenderer::IsEnabled();
        InspectorRenderer::DrawContext context;
        json prefabParameters;
        if (reflected) {
            IComponent* const raw = &component;
            context.label = std::string(GetName()) + " の " + raw->GetInspectorName();
            context.owner = raw;
            context.objects = objectManager_;
            context.onChanged = [raw](const Reflection::PropertyDescriptor& property) {
                raw->OnPropertyChanged(property);
                };
            context.defaultParameters = factory.GetDefaultParameters(typeName);

            // プレハブから作ったオブジェクトは、プレハブでのこのコンポーネントの値と見比べる
            if (IsPrefabInstance()) {
                const json* prefabComponents = PrefabSystem::LoadComponents(GetPrefab().GetValue());
                const std::optional<std::size_t> prefabIndex = prefabComponents
                    ? PrefabSystem::FindComponentIndex(*prefabComponents, *this, *raw) : std::nullopt;
                if (prefabIndex) {
                    const json& entry = (*prefabComponents)[*prefabIndex];
                    prefabParameters = (entry.contains("parameters") && entry.at("parameters").is_object())
                        ? entry.at("parameters") : json::object();
                    context.prefabParameters = &prefabParameters;
                    if (objectManager_) {
                        context.applyToPrefab = [this, raw](const Reflection::PropertyDescriptor& property) {
                            PrefabEditing::ApplyProperty(*objectManager_, *this, *raw, property);
                            };
                    }
                }
            }
        }

        // ⋮ と右クリックのメニュー
        if (ImGui::BeginPopup(kComponentMenuId)) {
            ImGui::TextDisabled("%s", typeName.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("既定値へ戻す", nullptr, false, reflected && context.defaultParameters)) {
                changed = InspectorRenderer::ResetToDefaults(*descriptor, component.GetReflectionInstance(), context)
                    || changed;
            }
            if (isScript && ImGui::MenuItem("スクリプトを開く", nullptr, false, !sourceFile.empty())) {
                Editor::OpenInCodeEditor(sourceFile);
            }
            ImGui::Separator();
            std::string reason;
            const bool removable = ComponentEditing::CanRemove(*this, component, &reason);
            if (ImGui::MenuItem("外す", nullptr, false, removable)) {
                removeRequest = &component;
            }
            if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", reason.c_str());
            }
            ImGui::EndPopup();
        }

        if (open) {
            // DrawInspector() の戻り値は「値が変わったか」であって
            // 「何か描いたか」ではない。中身の有無はカーソルが進んだかで見る
            const float cursorBefore = ImGui::GetCursorPosY();

            // 記述子を持つ型はそこから自動生成し、無ければ従来の手書きへ落ちる
            if (reflected) {
                IComponent* const raw = &component;
                changed |= InspectorRenderer::Draw(*descriptor, raw->GetReflectionInstance(), context);
                // 記述子に一部だけを載せた型は、残りを手書きの UI で描く
                if (descriptor->partial) {
                    changed |= raw->DrawInspector();
                }
                raw->DrawInspectorExtra();
            } else {
                changed |= component.DrawInspector();
            }

            if (ImGui::GetCursorPosY() <= cursorBefore) {
                UI::Hint("編集できる項目はありません");
            }

            if (isScript) {
                InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kOpenScriptLabel));
                const bool canOpen = !sourceFile.empty();
                if (UI::Bar::Button(kOpenScriptLabel, false,
                        canOpen ? "VS Code で開く" : "スクリプトのファイルが分かりません", canOpen)) {
                    Editor::OpenInCodeEditor(sourceFile);
                }
            }
            ImGui::Spacing();
        }

        ImGui::PopID();
        return changed;
    }

    bool GameObject::DrawImGui() {
        bool changed = false;
        ImGui::PushID(this);

        changed |= DrawInspectorHeader();
        ImGui::Spacing();

        // コンポーネントのセクション（トランスフォーム系を先頭に並べる。外すのは全部を描き終えてから行う）
        IComponent* removeRequest = nullptr;
        for (const bool firstPass : { true, false }) {
            for (const auto& component : GetAllComponents()) {
                if (!component || !component->IsShownInInspector()) {
                    continue;
                }
                if (component->IsShownFirstInInspector() == firstPass) {
                    changed |= DrawComponentSection(*component, removeRequest);
                }
            }
        }
        if (removeRequest && ComponentEditing::Remove(*this, *removeRequest)) {
            changed = true;
        }

        // コンポーネント追加（右寄せ）
        ImGui::Spacing();
        if (const std::string addType = ComponentEditing::DrawAddButton(*this); !addType.empty()) {
            changed |= ComponentEditing::Add(*this, addType) != nullptr;
        }

        ImGui::PopID();
        return changed;
    }
#endif // USE_IMGUI

}


