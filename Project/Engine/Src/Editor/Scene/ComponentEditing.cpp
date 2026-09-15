#include "pch.h"
#include "Editor/Scene/ComponentEditing.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
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
        constexpr const char* kRemoveButtonLabel = "外す";
        constexpr float kDisplayNameWidth = 160.0f;
        constexpr float kFilterWidth = 280.0f;

        /// @brief 足せる型の一覧を絞り込む文字列
        char sAddFilter[64] = "";

        /// @brief 付け外ししたコンポーネントと、その位置
        struct Slot
        {
            IComponent* component = nullptr;
            std::size_t position = 0;
        };

        /// @brief コンポーネントの付け外しを戻す・やり直すコマンド
        /// @details 外した実体はオブジェクトが控えているので、同じ実体を同じ位置へ付け直す。
        class AttachmentCommand final : public Editor::IEditorCommand
        {
        public:
            /// @param attachOnRedo やり直したときに付いた状態になるか（足す操作は true、外す操作は false）
            AttachmentCommand(std::string label, GameObjectManager& manager, ObjectId objectId,
                              std::vector<Slot> slots, bool attachOnRedo)
                : label_(std::move(label)), manager_(&manager), objectId_(objectId),
                  slots_(std::move(slots)), attachOnRedo_(attachOnRedo) {}

            void Undo() override { Apply(!attachOnRedo_); }
            void Redo() override { Apply(attachOnRedo_); }
            std::string GetLabel() const override { return label_; }

            bool References(const void* target) const override
            {
                return target != nullptr && std::any_of(slots_.begin(), slots_.end(),
                    [target](const Slot& slot) { return slot.component == target; });
            }

        private:
            void Apply(bool attach)
            {
                GameObject* object = manager_->FindObject(objectId_);
                if (!object) {
                    return;
                }

                // 付けるときは前の位置から、外すときは後ろから行う
                if (attach) {
                    for (const Slot& slot : slots_) {
                        object->ReattachComponent(slot.component, slot.position);
                    }
                } else {
                    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
                        object->DetachComponent(it->component);
                    }
                }
                manager_->InvalidateReferences();
            }

            std::string label_;
            GameObjectManager* manager_ = nullptr;
            ObjectId objectId_{};
            std::vector<Slot> slots_;
            bool attachOnRedo_ = true;
        };

        /// @brief 型名に対応するインスペクタでの表示名（無ければ型名）
        std::string DisplayNameOf(const std::string& typeName)
        {
            std::string name = ComponentFactory::Get().GetInspectorName(typeName);
            return name.empty() ? typeName : name;
        }

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

        /// @brief 幅 width の項目を、今の行の右端へ寄せる
        void AlignToRight(float width)
        {
            const float available = ImGui::GetContentRegionAvail().x;
            if (available > width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
            }
        }

        /// @brief ラベルを収めるボタンの幅
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
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
                slots.push_back(Slot{ component, *position });
            }
        }

        const std::string displayName = added->GetInspectorName();
        if (GameObjectManager* manager = object.GetObjectManager()) {
            manager->InvalidateReferences();
            Editor::EditorCommandStack::Get().Push(std::make_unique<AttachmentCommand>(
                object.GetName() + " に" + displayName + "を追加", *manager, object.GetObjectId(),
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
                *reason = std::string("「") + slot->GetInspectorName() + "」が使っているので外せません";
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

        const std::optional<std::size_t> position = object.DetachComponent(&component);
        if (!position) {
            return false;
        }

        const std::string displayName = component.GetInspectorName();
        if (GameObjectManager* manager = object.GetObjectManager()) {
            manager->InvalidateReferences();
            Editor::EditorCommandStack::Get().Push(std::make_unique<AttachmentCommand>(
                object.GetName() + " から" + displayName + "を外す", *manager, object.GetObjectId(),
                std::vector<Slot>{ Slot{ &component, *position } }, false));
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ComponentEditing: \"{}\" から {}（{}）を外しました", object.GetName(), displayName,
            component.GetTypeName());
        return true;
    }

    std::string DrawAddButton(const GameObject& object)
    {
        AlignToRight(ButtonWidth(kAddButtonLabel));
        if (ImGui::Button(kAddButtonLabel)) {
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

            int shown = 0;
            for (const std::string& typeName : ComponentFactory::Get().GetRegisteredTypeNames()) {
                const std::string displayName = DisplayNameOf(typeName);
                if (!ContainsIgnoreCase(displayName, sAddFilter) && !ContainsIgnoreCase(typeName, sAddFilter)) {
                    continue;
                }
                ++shown;

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
            if (shown == 0) {
                UI::Hint("該当する型がありません");
            }
        }
        return chosen;
    }

    bool DrawRemoveButton(const GameObject& object, const IComponent& component)
    {
        std::string reason;
        const bool removable = CanRemove(object, component, &reason);

        AlignToRight(ButtonWidth(kRemoveButtonLabel));
        bool clicked = false;
        {
            UI::Scope::DisabledScope disabled(!removable);
            clicked = ImGui::SmallButton(kRemoveButtonLabel);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", removable ? "このコンポーネントを外す（Ctrl+Z で戻せる）" : reason.c_str());
        }
        return clicked && removable;
    }
}

#endif // USE_IMGUI
