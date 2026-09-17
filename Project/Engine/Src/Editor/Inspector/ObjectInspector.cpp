#include "pch.h"
#include "Editor/Inspector/ObjectInspector.h"

#ifdef USE_IMGUI

#include "Editor/External/ExternalCodeEditor.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "Editor/Scene/ComponentEditing.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/PrefabEditing.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/PrefabSystem.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

namespace CoreEngine::Editor::ObjectInspector
{
    namespace
    {
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

        /// @brief 先頭の行（種類の記号・有効・名前・⋮）と、プレハブの行を描く
        /// @return 値が変更されたら true
        bool DrawHeader(GameObject& object, const Callbacks& callbacks)
        {
            bool changed = false;
            const ImGuiStyle& style = ImGui::GetStyle();

            // 種類の記号（プレハブから作ったものは ◈）
            ImGui::AlignTextToFramePadding();
            if (object.IsPrefabInstance()) {
                ImGui::TextColored(Theme::kAccentHover, "◈");
            } else {
                ImGui::TextColored(Theme::kTextDim, "◆");
            }

            // 有効
            ImGui::SameLine();
            bool active = object.IsActive();
            if (ImGui::Checkbox("##active", &active)) {
                object.SetActive(active);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", active ? "有効（外すと更新も描画もしない）" : "無効（入れると動く）");
            }

            // 名前（右端に ⋮ の場所を残す）
            const float menuWidth = ImGui::CalcTextSize("⋮").x + style.FramePadding.x * 2.0f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - menuWidth - style.ItemSpacing.x));
            char nameBuf[128];
            const std::string& shownName = object.GetName().empty() ? object.GetSerializeKey() : object.GetName();
            snprintf(nameBuf, sizeof(nameBuf), "%s", shownName.c_str());
            if (ImGui::InputText("##objName", nameBuf, sizeof(nameBuf))) {
                object.SetName(nameBuf);
                changed = true;
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::kTransparent);
            if (ImGui::Button("⋮##objectMenuButton", ImVec2(menuWidth, 0.0f))) {
                ImGui::OpenPopup(kObjectMenuId);
            }
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup(kObjectMenuId)) {
                const bool canSave = object.IsSerializeEnabled() && !object.GetSerializeKey().empty() &&
                    static_cast<bool>(callbacks.saveObject);
                if (ImGui::MenuItem("このオブジェクトだけ保存", nullptr, false, canSave)) {
                    callbacks.saveObject(object);
                }
                ImGui::EndPopup();
            }

            // プレハブから作ったものは、元のプレハブを参照の欄で出す
            if (object.IsPrefabInstance()) {
                const std::string path = object.GetPrefab().GetPath();
                const bool labelHovered = InspectorLayout::BeginRow("Prefab", Theme::kTextDim, kPrefabMenuId);
                const bool fieldHovered = InspectorLayout::ReferenceField(
                    InspectorLayout::AssetGlyph(AssetType::Prefab), StemOf(path).c_str(),
                    InspectorLayout::ShortId(object.GetPrefab().GetGuid()).c_str());
                ImGui::OpenPopupOnItemClick(kPrefabMenuId, ImGuiPopupFlags_MouseButtonRight);
                if (labelHovered || fieldHovered) {
                    ImGui::SetTooltip("%s\n右クリックでプレハブの操作", path.c_str());
                }
                if (ImGui::BeginPopup(kPrefabMenuId)) {
                    ImGui::TextDisabled("%s", path.c_str());
                    ImGui::Separator();
                    GameObjectManager* const manager = object.GetObjectManager();
                    if (ImGui::MenuItem("プレハブへ適用", nullptr, false, manager != nullptr)) {
                        PrefabEditing::ApplyObject(*manager, object);
                    }
                    if (ImGui::MenuItem("プレハブとのつながりを外す")) {
                        PrefabEditing::Unlink(object);
                    }
                    ImGui::EndPopup();
                }
            }
            return changed;
        }

        /// @brief コンポーネント 1 個のセクション（見出しと中身）を描く
        /// @param removeRequest 「外す」が選ばれたら、そのコンポーネントを書く先（選ばれなければ書かない）
        /// @return 値が変更されたら true
        bool DrawComponentSection(GameObject& object, IComponent& component, IComponent*& removeRequest)
        {
            bool changed = false;
            ImGui::PushID(&component);

            ComponentFactory& factory = ComponentFactory::Get();
            const std::string typeName = component.GetTypeName();
            const std::string displayName = ComponentInspectors::DisplayNameOf(component);
            const ComponentInspectors::Entry* const inspector = ComponentInspectors::Find(component);
            const bool isScript = factory.IsRuntimeType(typeName);
            const std::filesystem::path sourceFile = isScript ? factory.GetSourceFile(typeName) : std::filesystem::path{};
            const std::string tag = sourceFile.empty() ? std::string("AS")
                : Logger::GetInstance().PathToUtf8(sourceFile.filename());

            // 見出し
            bool enabled = component.IsEnabled();
            bool enabledChanged = false;
            InspectorLayout::SectionHeader header;
            header.name = displayName.c_str();
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
            const Reflection::TypeDescriptor* const descriptor = component.GetTypeDescriptor();
            GameObjectManager* const manager = object.GetObjectManager();
            InspectorRenderer::DrawContext context;
            json prefabParameters;
            if (descriptor) {
                IComponent* const raw = &component;
                context.label = object.GetName() + " の " + displayName;
                context.owner = raw;
                context.objects = manager;
                context.onChanged = [raw](const Reflection::PropertyDescriptor& property) {
                    raw->OnPropertyChanged(property);
                    };
                context.resolveComponent = [handle = ComponentHandle::Of(*raw)] {
                    return handle.Resolve();
                    };
                context.defaultParameters = factory.GetDefaultParameters(typeName);

                // プレハブから作ったオブジェクトは、プレハブでのこのコンポーネントの値と見比べる
                if (object.IsPrefabInstance()) {
                    const json* const prefabComponents = PrefabSystem::LoadComponents(object.GetPrefab().GetValue());
                    const std::optional<std::size_t> prefabIndex = prefabComponents
                        ? PrefabSystem::FindComponentIndex(*prefabComponents, object, *raw) : std::nullopt;
                    if (prefabIndex) {
                        const json& entry = (*prefabComponents)[*prefabIndex];
                        prefabParameters = (entry.contains("parameters") && entry.at("parameters").is_object())
                            ? entry.at("parameters") : json::object();
                        context.prefabParameters = &prefabParameters;
                        if (manager) {
                            GameObject* const target = &object;
                            context.applyToPrefab = [manager, target, raw](const Reflection::PropertyDescriptor& property) {
                                PrefabEditing::ApplyProperty(*manager, *target, *raw, property);
                                };
                        }
                    }
                }
            }

            // ⋮ と右クリックのメニュー
            if (ImGui::BeginPopup(kComponentMenuId)) {
                ImGui::TextDisabled("%s", typeName.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("既定値へ戻す", nullptr, false, descriptor && context.defaultParameters)) {
                    changed = InspectorRenderer::ResetToDefaults(*descriptor, component.GetReflectionInstance(), context)
                        || changed;
                }
                if (isScript && ImGui::MenuItem("スクリプトを開く", nullptr, false, !sourceFile.empty())) {
                    OpenInCodeEditor(sourceFile);
                }
                ImGui::Separator();
                std::string reason;
                const bool removable = ComponentEditing::CanRemove(object, component, &reason);
                if (ImGui::MenuItem("外す", nullptr, false, removable)) {
                    removeRequest = &component;
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
                // 記述子で描けない欄（記述子を持たない型は全部、一部だけを載せた型は残り）
                if (inspector && inspector->drawBody && (!descriptor || descriptor->partial)) {
                    changed |= inspector->drawBody(component);
                }
                if (inspector && inspector->drawExtra) {
                    inspector->drawExtra(component);
                }

                if (ImGui::GetCursorPosY() <= cursorBefore) {
                    UI::Hint("編集できる項目はありません");
                }

                if (isScript) {
                    InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kOpenScriptLabel));
                    const bool canOpen = !sourceFile.empty();
                    if (UI::Bar::Button(kOpenScriptLabel, false,
                            canOpen ? "VS Code で開く" : "スクリプトのファイルが分かりません", canOpen)) {
                        OpenInCodeEditor(sourceFile);
                    }
                }
                ImGui::Spacing();
            }

            ImGui::PopID();
            return changed;
        }
    }

    bool Draw(GameObject& object, const Callbacks& callbacks)
    {
        bool changed = false;
        ImGui::PushID(&object);

        // 破棄待ちのオブジェクトは赤で出す
        const bool marked = object.IsMarkedForDestroy();
        if (marked) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kError);
        }

        changed |= DrawHeader(object, callbacks);
        ImGui::Spacing();

        // コンポーネントのセクション（トランスフォーム系を先頭に並べる。外すのは全部を描き終えてから行う）
        IComponent* removeRequest = nullptr;
        for (const bool firstPass : { true, false }) {
            for (const auto& component : object.GetAllComponents()) {
                if (!component || !ComponentInspectors::IsShown(*component)) {
                    continue;
                }
                if (ComponentInspectors::IsShownFirst(*component) == firstPass) {
                    changed |= DrawComponentSection(object, *component, removeRequest);
                }
            }
        }
        if (removeRequest && ComponentEditing::Remove(object, *removeRequest)) {
            changed = true;
        }

        // コンポーネント追加（右寄せ）
        ImGui::Spacing();
        if (const std::string addType = ComponentEditing::DrawAddButton(object); !addType.empty()) {
            changed |= ComponentEditing::Add(object, addType) != nullptr;
        }

        if (marked) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        return changed;
    }
}

#endif // USE_IMGUI
