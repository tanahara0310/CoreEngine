#include "pch.h"
#include "Editor/Scene/ComponentEditing.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace CoreEngine::ComponentEditing
{
    namespace
    {
        constexpr const char* kAddButtonLabel = "＋ コンポーネント追加";
        constexpr const char* kAddPopupId = "##AddComponentPopup";
        constexpr float kDisplayNameWidth = 160.0f;
        constexpr float kFilterWidth = 280.0f;

        /// @brief 足せる型の一覧を絞り込む文字列
        char sAddFilter[64] = "";

        /// @brief 付け外しするコンポーネント 1 つ分
        struct Slot
        {
            std::string type;              ///< 型名
            std::size_t position = 0;      ///< 取り外し済みを除いた並びでの位置
            json entry;                    ///< 作り直すときの保存形
            IComponent* instance = nullptr; ///< 最後に付け外しした実体（控えの中を探すときに比べるだけに使う）
        };

        /// @brief 付け外ししたコンポーネントの控えを作る
        Slot MakeSlot(IComponent& component, std::size_t position)
        {
            return Slot{ component.GetTypeName(), position, ComponentHost::SerializeComponent(component), &component };
        }

        /// @brief コンポーネントの付け外しを戻す・やり直すコマンド
        /// @details 相手のオブジェクトは今のシーンから ID で引く。外して控えている実体があればそれを
        ///          同じ位置へ付け直し、無ければ（シーンを組み直した後など）保存形から作り直す。
        class AttachmentCommand final : public Editor::IEditorCommand
        {
        public:
            /// @param attachOnRedo やり直したときに付いた状態になるか（足す操作は true、外す操作は false）
            AttachmentCommand(std::string label, ObjectId objectId, std::vector<Slot> slots, bool attachOnRedo)
                : label_(std::move(label)), objectId_(objectId),
                  slots_(std::move(slots)), attachOnRedo_(attachOnRedo) {}

            void Undo() override { Apply(!attachOnRedo_); }
            void Redo() override { Apply(attachOnRedo_); }
            std::string GetLabel() const override { return label_; }
            bool SurvivesSceneReload() const override { return true; }

        private:
            void Apply(bool attach)
            {
                GameObjectManager* const manager = Editor::SceneAccess::Objects();
                GameObject* const object = manager ? manager->FindObject(objectId_) : nullptr;
                if (!object) {
                    return;
                }

                // 付けるときは前の位置から、外すときは後ろから行う
                if (attach) {
                    for (Slot& slot : slots_) {
                        IComponent* const detached = object->FindDetachedComponent(slot.instance);
                        if (detached && slot.type == detached->GetTypeName() &&
                            object->ReattachComponent(detached, slot.position)) {
                            continue;
                        }
                        slot.instance = object->RestoreComponent(slot.entry, slot.position);
                    }
                } else {
                    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
                        IComponent* const attached = FindAttached(*object, *it);
                        if (!attached) {
                            continue;
                        }
                        it->entry = ComponentHost::SerializeComponent(*attached);
                        object->DetachComponent(attached);
                        it->instance = attached;
                    }
                }
                manager->InvalidateReferences();
            }

            /// @brief 付いているコンポーネントのうち、控えに当たるものを探す
            /// @details 最後に付けた実体が付いていればそれ、無ければ控えの位置にある同じ型、
            ///          それも無ければ最初の同じ型を返す。
            static IComponent* FindAttached(const GameObject& object, const Slot& slot)
            {
                IComponent* sameType = nullptr;
                std::size_t position = 0;
                for (const auto& component : object.GetAllComponents()) {
                    if (!component) {
                        continue;
                    }
                    if (component.get() == slot.instance && slot.type == component->GetTypeName()) {
                        return component.get();
                    }
                    if (slot.type == component->GetTypeName()) {
                        if (position == slot.position) {
                            sameType = component.get();
                        } else if (!sameType) {
                            sameType = component.get();
                        }
                    }
                    ++position;
                }
                return sameType;
            }

            std::string label_;
            ObjectId objectId_{};
            std::vector<Slot> slots_;
            bool attachOnRedo_ = true;
        };

        /// @brief 英字の大小を区別せずに部分一致を調べる
        bool ContainsIgnoreCase(std::string_view text, std::string_view pattern)
        {
            if (pattern.empty()) {
                return true;
            }
            const auto toLower = [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            };
            return std::search(text.begin(), text.end(), pattern.begin(), pattern.end(),
                [&toLower](char a, char b) { return toLower(a) == toLower(b); }) != text.end();
        }

        /// @brief 追加の一覧の分類 1 つ
        struct Category
        {
            const char* label;              ///< タブと見出しに出す名前
            std::vector<std::string> types; ///< 並べる型名（この順に出す）
        };

        /// @brief エンジンのコンポーネントの分類（型名の並びがそのまま一覧の並び）
        const std::vector<Category>& EngineCategories()
        {
            static const std::vector<Category> categories = {
                { "基本", { "Transform", "EulerTransform", "Camera" } },
                { "描画", { "MeshRenderer", "Material", "SpriteRenderer", "Text3DRenderer", "Animator", "SkeletonSocket" } },
                { "環境", { "Light", "SkyBox", "VolumetricCloud", "HeightFog", "PostProcess", "WaterSurface" } },
                { "エフェクト", { "ParticleSystem", "GpuParticleSystem" } },
                { "物理", { "Collider", "Rigidbody", "CharacterController", "PhysicsMaterial" } },
                { "UI", { "RectTransform", "UIImage", "UIText", "UIButton", "UISlider", "UIToggle" } },
                { "音", { "AudioSource", "AudioListener" } },
            };
            return categories;
        }

        /// @brief 登録済みの型を分類に振り分ける
        /// @details エンジンの分類に無いエンジンの型は「その他」、スクリプトのクラスは「スクリプト」へ入れる。
        ///          型が 1 つも無い分類は含めない。
        std::vector<Category> BuildAddCategories()
        {
            const ComponentFactory& factory = ComponentFactory::Get();
            std::vector<Category> result;
            std::vector<std::string> placed;
            for (const Category& category : EngineCategories()) {
                Category filled{ category.label, {} };
                for (const std::string& type : category.types) {
                    if (factory.IsRegistered(type) && !factory.IsRuntimeType(type)) {
                        filled.types.push_back(type);
                        placed.push_back(type);
                    }
                }
                if (!filled.types.empty()) {
                    result.push_back(std::move(filled));
                }
            }

            Category others{ "その他", {} };
            Category scripts{ "スクリプト", {} };
            for (const std::string& type : factory.GetRegisteredTypeNames()) {
                if (factory.IsRuntimeType(type)) {
                    scripts.types.push_back(type);
                } else if (std::find(placed.begin(), placed.end(), type) == placed.end()) {
                    others.types.push_back(type);
                }
            }
            for (Category* rest : { &others, &scripts }) {
                if (!rest->types.empty()) {
                    result.push_back(std::move(*rest));
                }
            }
            return result;
        }

        /// @brief 一覧の 1 行を描き、選ばれたら型名を chosen に入れる
        void DrawAddItem(const GameObject& object, const std::string& typeName, std::string& chosen)
        {
            const std::string displayName = Editor::ComponentInspectors::DisplayNameOf(typeName);
            std::string reason;
            const bool addable = CanAdd(object, typeName, &reason);
            const std::string label = displayName + "##" + typeName;
            {
                UI::Scope::DisabledScope disabled(!addable);
                if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(kDisplayNameWidth, 0.0f))) {
                    chosen = typeName;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!addable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", reason.c_str());
            }
            UI::SameLine();
            UI::Hint(typeName.c_str());
        }

        /// @brief 型の表示名か型名が検索の文字を含むか
        bool MatchesFilter(const std::string& typeName)
        {
            return ContainsIgnoreCase(Editor::ComponentInspectors::DisplayNameOf(typeName), sAddFilter)
                || ContainsIgnoreCase(typeName, sAddFilter);
        }
    }

    bool CanAdd(const GameObject& object, const std::string& typeName, std::string* reason)
    {
        const auto fail = [reason](const char* message) {
            if (reason) {
                *reason = message;
            }
            return false;
        };

        if (!ComponentFactory::Get().IsRegistered(typeName)) {
            return fail("型名から作れない型です");
        }
        for (const auto& slot : object.GetAllComponents()) {
            if (slot && typeName == slot->GetTypeName()) {
                return fail("同じ型のコンポーネントがすでに付いています");
            }
        }

        // 描画のパスとブレンドは 1 つ目の描画コンポーネントで決まるので、2 つ目は付けない
        if (ComponentFactory::Get().IsRenderableType(typeName)) {
            for (const auto& slot : object.GetAllComponents()) {
                if (slot && ComponentFactory::IsRenderable(*slot)) {
                    return fail("描画するコンポーネントは 1 つのオブジェクトに 1 つまでです");
                }
            }
        }
        return true;
    }

    IComponent* Add(GameObject& object, const std::string& typeName)
    {
        std::string reason;
        if (!CanAdd(object, typeName, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ComponentEditing: \"{}\" に {} を足せません: {}", object.GetName(), typeName, reason);
            return nullptr;
        }

        // Awake の中で足されるものも含めて、この操作で付いたものを末尾から拾う
        const std::size_t firstSlot = object.GetAllComponents().size();
        IComponent* added = nullptr;
        {
            ComponentHost::DataAttachScope dataScope(object);
            added = object.AttachComponent(ComponentFactory::Get().Create(typeName));
        }
        if (!added) {
            return nullptr;
        }

        std::vector<Slot> slots;
        const auto& components = object.GetAllComponents();
        for (std::size_t i = firstSlot; i < components.size(); ++i) {
            IComponent* component = components[i].get();
            if (!component) {
                continue;
            }
            if (const std::optional<std::size_t> position = object.FindComponentPosition(component)) {
                slots.push_back(MakeSlot(*component, *position));
            }
        }

        const std::string displayName = Editor::ComponentInspectors::DisplayNameOf(*added);
        if (GameObjectManager* manager = object.GetObjectManager()) {
            manager->InvalidateReferences();
            Editor::EditorCommandStack::Get().Push(std::make_unique<AttachmentCommand>(
                object.GetName() + " に" + displayName + "を追加", object.GetObjectId(),
                std::move(slots), true));
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ComponentEditing: \"{}\" に {}（{}）を足しました", object.GetName(), displayName, typeName);
        return added;
    }

    bool CanRemove(const GameObject& object, const IComponent& component, std::string* reason)
    {
        if (component.IsAttachedByCode()) {
            if (reason) {
                *reason = "コードが付けたコンポーネントは外せません（外して保存しても、次の起動でコードが付け直します）";
            }
            return false;
        }

        for (const auto& slot : object.GetAllComponents()) {
            if (!slot || slot.get() == &component || !slot->RequiresComponent(component)) {
                continue;
            }
            if (reason) {
                *reason = "「" + Editor::ComponentInspectors::DisplayNameOf(*slot) + "」が使っているので外せません";
            }
            return false;
        }
        return true;
    }

    bool Remove(GameObject& object, IComponent& component)
    {
        std::string reason;
        if (!CanRemove(object, component, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ComponentEditing: \"{}\" の {} を外せません: {}", object.GetName(), component.GetTypeName(), reason);
            return false;
        }

        Slot slot = MakeSlot(component, 0);
        const std::optional<std::size_t> position = object.DetachComponent(&component);
        if (!position) {
            return false;
        }
        slot.position = *position;

        const std::string displayName = Editor::ComponentInspectors::DisplayNameOf(component);
        if (GameObjectManager* manager = object.GetObjectManager()) {
            manager->InvalidateReferences();
            Editor::EditorCommandStack::Get().Push(std::make_unique<AttachmentCommand>(
                object.GetName() + " から" + displayName + "を外す", object.GetObjectId(),
                std::vector<Slot>{ std::move(slot) }, false));
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ComponentEditing: \"{}\" から {}（{}）を外しました", object.GetName(), displayName,
            component.GetTypeName());
        return true;
    }

    std::string DrawAddButton(const GameObject& object)
    {
        InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kAddButtonLabel));
        if (UI::Bar::Button(kAddButtonLabel, false)) {
            sAddFilter[0] = '\0';
            ImGui::OpenPopup(kAddPopupId);
        }

        std::string chosen;
        if (auto popup = UI::Scope::PopupScope(kAddPopupId)) {
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(kFilterWidth);
            ImGui::InputTextWithHint("##filter", "型を検索", sAddFilter, sizeof(sAddFilter));
            UI::Separator();

            const std::vector<Category> categories = BuildAddCategories();
            if (sAddFilter[0] == '\0') {
                // 検索していないときは分類ごとのタブに分ける
                if (ImGui::BeginTabBar("##AddComponentTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                    for (const Category& category : categories) {
                        if (ImGui::BeginTabItem(category.label)) {
                            for (const std::string& typeName : category.types) {
                                DrawAddItem(object, typeName, chosen);
                            }
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                }
            } else {
                // 検索しているときは、当てはまるものを分類の見出しつきで全部並べる
                int shown = 0;
                for (const Category& category : categories) {
                    bool headed = false;
                    for (const std::string& typeName : category.types) {
                        if (!MatchesFilter(typeName)) {
                            continue;
                        }
                        if (!headed) {
                            ImGui::SeparatorText(category.label);
                            headed = true;
                        }
                        DrawAddItem(object, typeName, chosen);
                        ++shown;
                    }
                }
                if (shown == 0) {
                    UI::Hint("該当する型がありません");
                }
            }
        }
        return chosen;
    }
}

#endif // CORE_EDITOR
