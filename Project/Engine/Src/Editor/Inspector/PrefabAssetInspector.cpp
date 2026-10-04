#include "pch.h"
#include "Editor/Inspector/PrefabAssetInspector.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/ComponentHost.h"
#include "GameObject/Component/Core/MissingComponent.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/PrefabSystem.h"
#include "Utility/Logger/Logger.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace CoreEngine::Editor::PrefabAssetInspector
{
    namespace
    {
        /// コンポーネントの ⋮ のメニュー
        constexpr const char* kComponentMenuId = "##prefabComponentMenu";

        /// 読み込んでいるプレハブ
        struct State
        {
            std::string guid;                                    ///< どのプレハブか
            json source;                                         ///< コンポーネントを作った元の components 配列
            std::vector<std::unique_ptr<IComponent>> components; ///< オブジェクトに付けずに作ったコンポーネント
            bool editing = false;                                ///< 編集の途中か
            json beforeEdit;                                     ///< 編集を始める前の components 配列
        };

        State& GetState()
        {
            static State state;
            return state;
        }

        /// @brief components 配列からコンポーネントを作り直す（オブジェクトには付けない）
        void Rebuild(State& state, const json& components)
        {
            state.components.clear();
            state.source = components;
            state.editing = false;
            for (const auto& entry : components) {
                if (!entry.is_object() || !entry.contains("type") || !entry["type"].is_string()) {
                    continue;
                }
                const std::string type = entry["type"].get<std::string>();
                std::unique_ptr<IComponent> component = ComponentFactory::Get().Create(type);
                if (!component) {
                    component = std::make_unique<MissingComponent>(type);
                }
                ComponentHost::LoadComponentEntry(*component, entry);
                state.components.push_back(std::move(component));
            }
        }

        /// @brief 今のコンポーネントを components 配列の形にする（シーン内の別オブジェクトへの参照は null にする）
        json MakeComponentsJson(const State& state)
        {
            json components = json::array();
            for (const auto& component : state.components) {
                json entry = ComponentHost::SerializeComponent(*component);
                if (entry.contains("parameters") && entry["parameters"].is_object()) {
                    for (auto& value : entry["parameters"]) {
                        if (value.is_object() && value.contains("ref")) {
                            value = nullptr;
                        }
                    }
                }
                components.push_back(std::move(entry));
            }
            return components;
        }

        /// @brief プレハブのファイルを書き換え、シーンでそのプレハブから作ったオブジェクトへ反映する
        bool WritePrefab(const Reflection::AssetRefValue& prefab, const json& components)
        {
            GameObjectManager* const manager = SceneAccess::Objects();
            if (!manager) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "PrefabAssetInspector: シーンが無いのでプレハブ \"{}\" へ書き込めません", prefab.path);
                return false;
            }
            if (!PrefabSystem::UpdatePrefab(*manager, prefab, components)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "PrefabAssetInspector: プレハブ \"{}\" へ書き込めませんでした", prefab.path);
                return false;
            }
            return true;
        }

        /// @brief このコンポーネントを外してよいか（トランスフォーム系と、ほかが必要としているものは外せない）
        bool CanRemove(const State& state, const IComponent& component, std::string* reason)
        {
            if (ComponentInspectors::IsShownFirst(component)) {
                *reason = "位置を持つコンポーネントは外せません";
                return false;
            }
            for (const auto& other : state.components) {
                if (other.get() != &component && other->RequiresComponent(component)) {
                    *reason = ComponentInspectors::DisplayNameOf(*other) + " が必要としています";
                    return false;
                }
            }
            return true;
        }

        /// @brief コンポーネント 1 個のセクション（見出しと中身）を描く
        /// @param removeRequest 「外す」が選ばれたら、その位置を書く先
        /// @return 値が変更されたら true
        bool DrawComponentSection(const State& state, const std::string& prefabName, std::size_t index,
                                  std::optional<std::size_t>& removeRequest)
        {
            IComponent& component = *state.components[index];
            bool changed = false;
            ImGui::PushID(static_cast<int>(index));

            ComponentFactory& factory = ComponentFactory::Get();
            const std::string typeName = component.GetTypeName();
            const std::string displayName = ComponentInspectors::DisplayNameOf(component);
            const ComponentInspectors::Entry* const inspector = ComponentInspectors::Find(component);
            const bool isScript = factory.IsRuntimeType(typeName);

            // 見出し
            bool enabled = component.IsEnabled();
            bool enabledChanged = false;
            InspectorLayout::SectionHeader header;
            header.name = displayName.c_str();
            header.origin = isScript ? InspectorLayout::Origin::Script : InspectorLayout::Origin::Native;
            header.enabled = &enabled;
            header.menuId = kComponentMenuId;
            const bool open = InspectorLayout::DrawSectionHeader(header, enabledChanged);
            if (enabledChanged) {
                component.SetEnabled(enabled);
                changed = true;
            }

            const Reflection::TypeDescriptor* const descriptor = component.GetTypeDescriptor();
            InspectorRenderer::DrawContext context;
            if (descriptor) {
                IComponent* const raw = &component;
                context.label = prefabName + " の " + displayName;
                context.owner = raw;
                context.onChanged = [raw](const Reflection::PropertyDescriptor& property) {
                    raw->OnPropertyChanged(property);
                    };
                context.defaultParameters = factory.GetDefaultParameters(typeName);
            }

            // ⋮ と右クリックのメニュー
            if (ImGui::BeginPopup(kComponentMenuId)) {
                ImGui::TextDisabled("%s", typeName.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("既定値へ戻す", nullptr, false, descriptor && context.defaultParameters)) {
                    changed = InspectorRenderer::ResetToDefaults(*descriptor, component.GetReflectionInstance(), context)
                        || changed;
                }
                ImGui::Separator();
                std::string reason;
                const bool removable = CanRemove(state, component, &reason);
                if (ImGui::MenuItem("外す", nullptr, false, removable)) {
                    removeRequest = index;
                }
                if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", reason.c_str());
                }
                ImGui::EndPopup();
            }

            if (open) {
                // 中身の有無は、値が変わったかではなくカーソルが進んだかで見る
                const float cursorBefore = ImGui::GetCursorPosY();
                if (descriptor) {
                    changed |= InspectorRenderer::Draw(*descriptor, component.GetReflectionInstance(), context);
                }
                if (inspector && inspector->drawBody && (!descriptor || descriptor->partial)) {
                    changed |= inspector->drawBody(component);
                }
                if (ImGui::GetCursorPosY() <= cursorBefore) {
                    UI::Hint(inspector && inspector->drawBody
                        ? "この部品の欄は、シーンに置いてから直してください（置いた物を直して「プレハブへ適用」）"
                        : "編集できる項目はありません");
                }
                ImGui::Spacing();
            }

            ImGui::PopID();
            return changed;
        }
    }

    bool Draw(const std::filesystem::path& file)
    {
        AssetDatabase& database = AssetDatabase::GetInstance();
        const std::string guid = database.GetGUID(file);
        const AssetInfo* const info = guid.empty() ? nullptr : database.FindAssetByGUID(guid);
        if (!info || info->type != AssetType::Prefab) {
            Release();
            return false;
        }
        const Reflection::AssetRefValue prefab{ info->guid, ToAssetPath(*info) };
        const std::string prefabName = info->name;

        UI::Separator();
        UI::SectionHeader("プレハブの中身");
        const json* const components = PrefabSystem::LoadComponents(prefab);
        if (!components) {
            Release();
            UI::Hint("プレハブを読めません");
            return true;
        }

        // 別のプレハブを選んだ・外で書き換わったときは作り直す（編集の途中は作り直さない）
        State& state = GetState();
        if (state.guid != guid || (!state.editing && *components != state.source)) {
            Rebuild(state, *components);
            state.guid = guid;
        }
        UI::Hint("変えた値はすぐプレハブに保存し、置いた物にも反映します");

        // 欄が積む Undo（シーンからコンポーネントを探すもの）は捨て、プレハブ全体の書き換えを 1 件として積む
        bool changed = false;
        std::optional<std::size_t> removeRequest;
        {
            DiscardScope discard;
            for (const bool firstPass : { true, false }) {
                for (std::size_t i = 0; i < state.components.size(); ++i) {
                    const IComponent& component = *state.components[i];
                    if (!ComponentInspectors::IsShown(component) ||
                        ComponentInspectors::IsShownFirst(component) != firstPass) {
                        continue;
                    }
                    changed |= DrawComponentSection(state, prefabName, i, removeRequest);
                }
            }
        }
        if (removeRequest) {
            state.components.erase(state.components.begin() + static_cast<std::ptrdiff_t>(*removeRequest));
            changed = true;
        }

        if (changed && !state.editing) {
            state.editing = true;
            state.beforeEdit = state.source;
        }

        // 編集を終えたら（ドラッグを離した・入力欄を離れた）書き戻す
        if (state.editing && !ImGui::IsAnyItemActive()) {
            state.editing = false;
            json before = std::move(state.beforeEdit);
            json after = MakeComponentsJson(state);
            if (!PrefabSystem::SameValue(before, after) && WritePrefab(prefab, after)) {
                state.source = after;
                EditorCommandStack::Get().Push(std::make_unique<FunctionCommand>(
                    prefabName + ".prefab を編集",
                    [prefab, before] { WritePrefab(prefab, before); },
                    [prefab, after] { WritePrefab(prefab, after); },
                    false, true));
            } else {
                Rebuild(state, state.source);
            }
        }
        return true;
    }

    void Release()
    {
        State& state = GetState();
        state.components.clear();
        state.source = json{};
        state.guid.clear();
        state.editing = false;
        state.beforeEdit = json{};
    }
}

#endif // CORE_EDITOR
