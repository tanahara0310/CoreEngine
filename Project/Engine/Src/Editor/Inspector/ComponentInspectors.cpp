#include "pch.h"
#include "Editor/Inspector/ComponentInspectors.h"

#ifdef CORE_EDITOR

#include "Editor/Inspector/ColliderInspector.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/IRawSavedParameters.h"
#include "GameObject/Component/Core/MissingComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/Component/Render/Text3DRendererComponent.h"
#include "GameObject/GameObject.h"
#include "Editor/ImGui/ParticleSystemDebugUI.h"
#include "Particle/Gpu/GpuParticleSystemComponent.h"
#include "Particle/ParticleSystemComponent.h"
#include "Reflection/TypeDescriptor.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace CoreEngine::Editor::ComponentInspectors
{
    namespace
    {
        /// @brief 型名 → 出し方
        std::unordered_map<std::string, Entry>& Entries()
        {
            static std::unordered_map<std::string, Entry> entries;
            return entries;
        }

        /// @brief 読み込めない型のコンポーネントの出し方（保存データをそのまま見せる）
        const Entry& MissingEntry()
        {
            static const Entry entry{
                .drawBody = [](IComponent& component) {
                    ImGui::TextWrapped("型「%s」が見つからないので、保存データをそのまま持っています。",
                        component.GetTypeName());
                    UI::Hint("スクリプトのコンパイルに失敗しているか、型の名前が変わったときに出ます。保存しても中身は消えません。");
                    const auto* const raw = dynamic_cast<const IRawSavedParameters*>(&component);
                    if (raw && !raw->GetRawParameters().empty()) {
                        UI::Separator();
                        ImGui::TextUnformatted(raw->GetRawParameters().dump(2).c_str());
                    }
                    return false;
                },
            };
            return entry;
        }

        /// @brief パーティクルのモジュールの欄を描き、編集が終わったら 1 回の Undo として積む
        template <class TSystem>
        bool DrawParticleModules(IComponent& component)
        {
            auto& system = static_cast<TSystem&>(component);

            // 同時に編集できる欄は 1 つなので、編集を始める前の値も 1 つだけ控える
            static const IComponent* editing = nullptr;
            static json before;

            const auto readModules = [](TSystem& target) {
                json modules = json::object();
                target.SaveModulesToJson(modules);
                return modules;
            };

            const bool alreadyEditing = editing == &component;
            json current = alreadyEditing ? json{} : readModules(system);
            const bool changed = ParticleSystemDebugUI::ShowImGui(system);
            if (changed && !alreadyEditing) {
                editing = &component;
                before = std::move(current);
            }
            if (editing == &component && !ImGui::IsAnyItemActive()) {
                editing = nullptr;
                const GameObject* const owner = component.GetOwner();
                std::string label = (owner ? owner->GetName() + " の " : std::string{}) + DisplayNameOf(component);
                EditorCommandStack::Get().Push(std::make_unique<ComponentStateCommand<TSystem, json>>(
                    std::move(label), system, std::move(before),
                    [](TSystem& target) {
                        json modules = json::object();
                        target.SaveModulesToJson(modules);
                        return modules;
                    },
                    [](TSystem& target, const json& settings) { target.LoadModulesFromJson(settings); }));
                before = json{};
            }
            return changed;
        }
    }

    void RegisterEngineTypes()
    {
        Register("Transform", { .shownFirst = true });
        Register("EulerTransform", { .shownFirst = true });
        Register("RectTransform", { .shownFirst = true });
        Register("SceneTag", { .hidden = true });
        Register("Animator", { .displayName = "アニメーション" });
        Register("SkeletonSocket", { .displayName = "ソケット追従" });
        Register("SkyBox", { .displayName = "スカイボックス" });

        Register("Collider", {
            .displayName = "コライダー",
            .drawBody = [](IComponent& component) {
                GameObject* const owner = component.GetOwner();
                return owner ? ColliderInspector::Draw(*owner) : false;
            },
            });

        Register("SpriteRenderer", {
            .displayName = "スプライト描画",
            .drawBody = [](IComponent& component) {
                return static_cast<SpriteRendererComponent&>(component).DrawEditorUI();
            },
            });

        Register("Text3DRenderer", {
            .displayName = "3D テキスト",
            .drawBody = [](IComponent& component) {
                return static_cast<Text3DRendererComponent&>(component).DrawEditorUI();
            },
            });

        Register("Material", {
            .drawExtra = [](IComponent& component) {
                if (!static_cast<MaterialComponent&>(component).GetMaterial()) {
                    UI::Hint("マテリアル未生成（メッシュの読み込み待ち）。編集した値は生成時に反映される");
                }
            },
            });

        Register("ParticleSystem", {
            .drawBody = [](IComponent& component) { return DrawParticleModules<ParticleSystemComponent>(component); },
            });
        Register("GpuParticleSystem", {
            .drawBody = [](IComponent& component) { return DrawParticleModules<GpuParticleSystemComponent>(component); },
            });
    }

    void Register(const std::string& typeName, Entry entry)
    {
        Entries()[typeName] = std::move(entry);
    }

    const Entry* Find(const IComponent& component)
    {
        if (dynamic_cast<const MissingComponent*>(&component)) {
            return &MissingEntry();
        }
        const auto found = Entries().find(component.GetTypeName());
        return found != Entries().end() ? &found->second : nullptr;
    }

    std::string DisplayNameOf(const IComponent& component)
    {
        if (dynamic_cast<const MissingComponent*>(&component)) {
            return std::string("読み込めない型: ") + component.GetTypeName();
        }
        const Entry* const entry = Find(component);
        if (entry && !entry->displayName.empty()) {
            return entry->displayName;
        }
        const Reflection::TypeDescriptor* const descriptor = component.GetTypeDescriptor();
        if (descriptor && descriptor->displayName && descriptor->displayName[0] != '\0') {
            return descriptor->displayName;
        }
        return component.GetTypeName();
    }

    std::string DisplayNameOf(const std::string& typeName)
    {
        const auto found = Entries().find(typeName);
        if (found != Entries().end() && !found->second.displayName.empty()) {
            return found->second.displayName;
        }
        const Reflection::TypeDescriptor* const descriptor = ComponentFactory::Get().FindDescriptor(typeName);
        if (descriptor && descriptor->displayName && descriptor->displayName[0] != '\0') {
            return descriptor->displayName;
        }
        return typeName;
    }

    bool IsShown(const IComponent& component)
    {
        const Entry* const entry = Find(component);
        return !entry || !entry->hidden;
    }

    bool IsShownFirst(const IComponent& component)
    {
        const Entry* const entry = Find(component);
        return entry && entry->shownFirst;
    }
}

#endif // CORE_EDITOR
