#include "pch.h"
#include "Editor/Inspector/InspectorRenderer.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/PropertyValue.h"
#include "Reflection/ReflectionToggle.h"
#include "Reflection/TypeDescriptor.h"
#include "Scene/PrefabSystem.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// @brief 記述子経由で編集した 1 プロパティを元に戻すコマンド
        class PropertyEditCommand final : public Editor::IEditorCommand
        {
        public:
            using Changed = std::function<void(const Reflection::PropertyDescriptor&)>;

            PropertyEditCommand(std::string label, const void* owner, void* instance,
                                const Reflection::PropertyDescriptor* property,
                                Reflection::PropertyValue before, Changed onChanged)
                : label_(std::move(label)), owner_(owner), instance_(instance),
                  property_(property), before_(std::move(before)), onChanged_(std::move(onChanged)) {}

            void Undo() override
            {
                // 積んだ時点では編集後の値が確定していないので、最初の Undo で控える
                if (!hasAfter_ && property_ && instance_) {
                    after_.LoadFrom(*property_, instance_);
                    hasAfter_ = after_.IsValid();
                }
                Apply(before_);
            }

            void Redo() override
            {
                if (hasAfter_) { Apply(after_); }
            }

            std::string GetLabel() const override { return label_; }

            bool References(const void* target) const override
            {
                return target != nullptr && (target == owner_ || target == instance_);
            }

        private:
            void Apply(const Reflection::PropertyValue& value)
            {
                if (!property_ || !instance_ || !value.StoreTo(*property_, instance_)) {
                    return;
                }
                if (onChanged_) { onChanged_(*property_); }
            }

            std::string label_;
            const void* owner_ = nullptr;
            void*       instance_ = nullptr;
            const Reflection::PropertyDescriptor* property_ = nullptr;
            Reflection::PropertyValue before_;
            Reflection::PropertyValue after_;
            bool    hasAfter_ = false;
            Changed onChanged_;
        };

        /// @brief 範囲指定からドラッグ速度を決める
        float ResolveSpeed(const Reflection::PropertyRange& range, float fallback)
        {
            if (range.speed > 0.0f) {
                return range.speed;
            }
            if (range.valid && range.max > range.min) {
                return (range.max - range.min) / 400.0f;
            }
            return fallback;
        }

        /// @brief 編集不可のプロパティを沈んだ表示で描く
        void DrawReadOnly(const Reflection::PropertyDescriptor& p, const void* value)
        {
            using Reflection::PropertyType;
            switch (p.type) {
            case PropertyType::Bool:
                ImGui::TextDisabled("%s: %s", p.displayName,
                    *static_cast<const bool*>(value) ? "true" : "false");
                break;
            case PropertyType::Int:
                ImGui::TextDisabled("%s: %d", p.displayName, *static_cast<const int*>(value));
                break;
            case PropertyType::Float:
                ImGui::TextDisabled("%s: %.3f", p.displayName, *static_cast<const float*>(value));
                break;
            case PropertyType::Vector2: {
                const auto& v = *static_cast<const Vector2*>(value);
                ImGui::TextDisabled("%s: %.3f, %.3f", p.displayName, v.x, v.y);
                break;
            }
            case PropertyType::Vector3: {
                const auto& v = *static_cast<const Vector3*>(value);
                ImGui::TextDisabled("%s: %.3f, %.3f, %.3f", p.displayName, v.x, v.y, v.z);
                break;
            }
            case PropertyType::Vector4:
            case PropertyType::Color: {
                const auto& v = *static_cast<const Vector4*>(value);
                ImGui::TextDisabled("%s: %.3f, %.3f, %.3f, %.3f", p.displayName, v.x, v.y, v.z, v.w);
                break;
            }
            case PropertyType::String:
                ImGui::TextDisabled("%s: %s", p.displayName,
                    static_cast<const std::string*>(value)->c_str());
                break;
            case PropertyType::ObjectRef: {
                const auto& ref = *static_cast<const Reflection::ObjectRefValue*>(value);
                ImGui::TextDisabled("%s: %s", p.displayName,
                    ref.objectId.IsValid() ? ref.objectId.ToString().c_str() : "（なし）");
                break;
            }
            case PropertyType::AssetRef: {
                const auto& ref = *static_cast<const Reflection::AssetRefValue*>(value);
                ImGui::TextDisabled("%s: %s", p.displayName,
                    ref.path.empty() ? "（なし）" : ref.path.c_str());
                break;
            }
            case PropertyType::Array:
                ImGui::TextDisabled("%s: %d 個", p.displayName,
                    static_cast<int>(static_cast<const Reflection::ArrayValue*>(value)->elements.size()));
                break;
            }
        }

        /// @brief 型に応じたウィジェットを 1 つ描く
        /// @param ownContextMenu 右クリックのメニューをこちらで出すか（色の編集欄の既定メニューを止める）
        bool DrawValueWidget(Reflection::PropertyType type, const char* label, const Reflection::PropertyRange& r,
                             void* value, bool ownContextMenu)
        {
            using Reflection::PropertyType;
            const float min = r.valid ? r.min : 0.0f;
            const float max = r.valid ? r.max : 0.0f;

            switch (type) {
            case PropertyType::Bool:
                return ImGui::Checkbox(label, static_cast<bool*>(value));
            case PropertyType::Int: {
                const int iMin = r.valid ? static_cast<int>(r.min) : 0;
                const int iMax = r.valid ? static_cast<int>(r.max) : 0;
                return ImGui::DragInt(label, static_cast<int*>(value),
                    ResolveSpeed(r, 1.0f), iMin, iMax);
            }
            case PropertyType::Float:
                return ImGui::DragFloat(label, static_cast<float*>(value),
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector2:
                return ImGui::DragFloat2(label, &static_cast<Vector2*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector3:
                return ImGui::DragFloat3(label, &static_cast<Vector3*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector4:
                return ImGui::DragFloat4(label, &static_cast<Vector4*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Color:
                return ImGui::ColorEdit4(label, &static_cast<Vector4*>(value)->x,
                    ownContextMenu ? ImGuiColorEditFlags_NoOptions : 0);
            case PropertyType::String: {
                auto* text = static_cast<std::string*>(value);
                char buffer[256]{};
                const size_t length = (std::min)(text->size(), sizeof(buffer) - 1);
                std::memcpy(buffer, text->data(), length);
                if (ImGui::InputText(label, buffer, sizeof(buffer))) {
                    *text = buffer;
                    return true;
                }
                return false;
            }
            case PropertyType::ObjectRef:
            case PropertyType::AssetRef:
            case PropertyType::Array:
                return false;
            }
            return false;
        }

        /// @brief プロパティの型に応じたウィジェットを描く
        /// @param ownContextMenu 右クリックのメニューをこちらで出すか（色の編集欄の既定メニューを止める）
        bool DrawWidget(const Reflection::PropertyDescriptor& p, void* value, bool ownContextMenu)
        {
            return DrawValueWidget(p.type, p.displayName, p.range, value, ownContextMenu);
        }

        /// @brief 配列の欄を描く（要素ごとの欄と、消す・並べ替える・足すボタン）
        /// @param structureChanged 要素を足す・消す・並べ替えたら true にする
        /// @return 要素の値か並びが変わったら true
        bool DrawArray(const Reflection::PropertyDescriptor& p, Reflection::ArrayValue& array,
                       bool ownContextMenu, bool& structureChanged)
        {
            std::vector<Reflection::ArrayValue::Element>& elements = array.elements;
            const bool open = ImGui::TreeNodeEx("##array", ImGuiTreeNodeFlags_SpanAvailWidth, "%s（%d 個）",
                p.displayName, static_cast<int>(elements.size()));
            if (!open) {
                return false;
            }

            bool edited = false;
            const size_t count = elements.size();
            size_t removeAt = count;
            size_t swapAt = count;
            for (size_t i = 0; i < elements.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("削除")) {
                    removeAt = i;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::SmallButton("上へ")) {
                    swapAt = i - 1;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 == elements.size());
                if (ImGui::SmallButton("下へ")) {
                    swapAt = i;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::Text("%d", static_cast<int>(i));
                if (void* const data = Reflection::ArrayElementData(p.elementType, elements[i])) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    edited = DrawValueWidget(p.elementType, "##value", p.range, data, ownContextMenu) || edited;
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("要素を追加")) {
                elements.push_back(Reflection::MakeArrayElement(p.elementType));
                structureChanged = true;
            }
            ImGui::TreePop();

            if (removeAt < count) {
                elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(removeAt));
                structureChanged = true;
            } else if (swapAt + 1 < count) {
                std::swap(elements[swapAt], elements[swapAt + 1]);
                structureChanged = true;
            }
            return edited || structureChanged;
        }

        /// @brief ObjectRef の値を、指定したオブジェクトの指せるコンポーネントへ向け直す
        /// @return 繋ぎ先が変わったら true
        bool Retarget(Reflection::ObjectRefValue& ref, const GameObject& object,
                      const Reflection::PropertyDescriptor& p)
        {
            const IComponent* component = FindReferencedComponent(object, p, {});
            if (!component) {
                return false;
            }

            Reflection::ObjectRefValue next{ object.GetObjectId(), component->GetTypeName() };
            if (next == ref) {
                return false;
            }
            ref = std::move(next);
            return true;
        }

        /// @brief ObjectRef の繋ぎ先を選ぶ欄を描く（候補の一覧と Hierarchy からのドロップ）
        /// @return 繋ぎ先が変わったら true
        bool DrawObjectRef(const Reflection::PropertyDescriptor& p, Reflection::ObjectRefValue& ref,
                           const GameObjectManager* objects)
        {
            const GameObject* target =
                (objects && ref.objectId.IsValid()) ? objects->FindObject(ref.objectId) : nullptr;
            const bool missing = ref.objectId.IsValid() &&
                (!target || !FindReferencedComponent(*target, p, ref.componentType));

            std::string preview = "（なし）";
            if (missing) {
                preview = "（見つかりません " + ref.objectId.ToString() + "）";
            } else if (target) {
                preview = target->GetDisplayName();
            }

            if (missing) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            }
            const bool open = ImGui::BeginCombo(p.displayName, preview.c_str());
            if (missing) {
                ImGui::PopStyleColor();
            }

            bool edited = false;
            if (!open) {
                // 閉じた欄へ Hierarchy のオブジェクトを落とすと繋ぎ替える
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(InspectorRenderer::kObjectDragPayload)) {
                        std::uint64_t droppedId = 0;
                        if (objects && payload->DataSize == sizeof(droppedId)) {
                            std::memcpy(&droppedId, payload->Data, sizeof(droppedId));
                            if (const GameObject* dropped = objects->FindObject(ObjectId{ droppedId })) {
                                edited = Retarget(ref, *dropped, p);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered() && ref.objectId.IsValid()) {
                    ImGui::SetTooltip("ID %s\n%s",
                        ref.objectId.ToString().c_str(), ref.componentType.c_str());
                }
                return edited;
            }

            if (ImGui::Selectable("（なし）", !ref.objectId.IsValid()) && ref.objectId.IsValid()) {
                ref = Reflection::ObjectRefValue{};
                edited = true;
            }
            if (objects) {
                for (const auto& object : objects->GetAllObjects()) {
                    if (!object || object->IsMarkedForDestroy() ||
                        !FindReferencedComponent(*object, p, {})) {
                        continue;
                    }

                    const bool selected = object->GetObjectId() == ref.objectId;
                    ImGui::PushID(object.get());
                    if (ImGui::Selectable(object->GetDisplayName(), selected)) {
                        edited = Retarget(ref, *object, p) || edited;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
            return edited;
        }

        /// @brief ProjectView からドラッグされるファイルのペイロード名（中身はファイル名）
        const char* FileDragPayloadOf(AssetType type)
        {
            switch (type) {
            case AssetType::Texture: return "TEXTURE_FILE";
            case AssetType::Model:   return "MODEL_FILE";
            case AssetType::Audio:   return "AUDIO_FILE";
            case AssetType::Prefab:  return "PREFAB_FILE";
            default:                 return nullptr;
            }
        }

        /// @brief ASCII の大文字と小文字を区別せずに部分一致するか
        bool ContainsIgnoreCase(std::string_view text, std::string_view pattern)
        {
            if (pattern.empty()) {
                return true;
            }
            const auto lower = [](char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            };
            const auto found = std::search(text.begin(), text.end(), pattern.begin(), pattern.end(),
                [&](char a, char b) { return lower(a) == lower(b); });
            return found != text.end();
        }

        /// @brief AssetRef の値を、指定したアセットへ向け直す
        /// @return 指す先が変わったら true
        bool RetargetAsset(Reflection::AssetRefValue& ref, const AssetInfo& info)
        {
            Reflection::AssetRefValue next{ info.guid, ToAssetPath(info) };
            if (next == ref) {
                return false;
            }
            ref = std::move(next);
            return true;
        }

        /// @brief AssetRef の指す先を選ぶ欄を描く（種類で絞った一覧と ProjectView からのドロップ）
        /// @return 指す先が変わったら true
        bool DrawAssetRef(const Reflection::PropertyDescriptor& p, Reflection::AssetRefValue& ref)
        {
            const AssetInfo* target = ResolveAssetRef(ref);
            const bool isSet = !ref.guid.empty() || !ref.path.empty();
            const bool invalid = isSet && (!target || target->type != p.assetType);

            std::string preview = "（なし）";
            if (isSet && !target) {
                preview = "（見つかりません " + (ref.path.empty() ? ref.guid : ref.path) + "）";
            } else if (invalid) {
                preview = "（種類が違います " + target->fileName + "）";
            } else if (target) {
                preview = target->fileName;
            }

            if (invalid) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            }
            const bool open = ImGui::BeginCombo(p.displayName, preview.c_str());
            if (invalid) {
                ImGui::PopStyleColor();
            }

            bool edited = false;
            if (!open) {
                // 閉じた欄へ ProjectView の同じ種類のファイルを落とすと指し直す
                const char* payloadType = FileDragPayloadOf(p.assetType);
                if (payloadType && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType)) {
                        const auto* data = static_cast<const char*>(payload->Data);
                        const std::string fileName(
                            data, strnlen(data, static_cast<size_t>(payload->DataSize)));
                        const AssetInfo* dropped = FindAssetInfo(fileName);
                        if (dropped && dropped->type == p.assetType) {
                            edited = RetargetAsset(ref, *dropped);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::IsItemHovered() && isSet) {
                    ImGui::SetTooltip("%s\nGUID %s", ref.path.c_str(),
                        ref.guid.empty() ? "（なし）" : ref.guid.c_str());
                }
                return edited;
            }

            // 一覧の先頭に絞り込み欄を置き、その種類のアセットだけを並べる
            static char filter[128] = "";
            if (ImGui::IsWindowAppearing()) {
                filter[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", "絞り込み", filter, sizeof(filter));

            if (ImGui::Selectable("（なし）", !isSet) && isSet) {
                ref = Reflection::AssetRefValue{};
                edited = true;
            }
            for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(p.assetType)) {
                const std::string path = ToAssetPath(*info);
                if (!ContainsIgnoreCase(path, filter)) {
                    continue;
                }

                const bool selected = info == target;
                ImGui::PushID(info->guid.c_str());
                if (ImGui::Selectable(info->fileName.c_str(), selected)) {
                    edited = RetargetAsset(ref, *info) || edited;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", path.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
            return edited;
        }

        /// @brief 直前の項目について、プレハブと違う値なら左に線を引き、右クリックでプレハブのメニューを出す
        /// @return プレハブの値に戻したら true
        bool DrawPrefabOverride(const Reflection::PropertyDescriptor& p, void* instance,
                                const InspectorRenderer::DrawContext& context, const std::string& ownerLabel)
        {
            if (!context.prefabParameters || !p.IsSaved()) {
                return false;
            }

            const json current = Reflection::PropertySerializer::PropertyToJson(p, instance);
            const auto base = context.prefabParameters->find(p.name);
            const bool hasBase = base != context.prefabParameters->end();
            const bool overridden = !hasBase || !PrefabSystem::SameValue(*base, current);

            if (overridden) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(min.x, min.y), ImVec2(min.x + 3.0f, max.y),
                    ImGui::GetColorU32(ImGuiCol_CheckMark));
            }

            bool reverted = false;
            if (ImGui::BeginPopupContextItem("##prefabOverride")) {
                if (ImGui::MenuItem("プレハブの値に戻す", nullptr, false, overridden && hasBase)) {
                    Reflection::PropertyValue before;
                    before.LoadFrom(p, instance);
                    if (Reflection::PropertySerializer::JsonToProperty(p, instance, *base)) {
                        if (context.onChanged) { context.onChanged(p); }
                        Editor::EditorCommandStack::Get().Push(
                            std::make_unique<PropertyEditCommand>(
                                ownerLabel + " の " + p.displayName + " をプレハブの値に戻す",
                                context.owner, instance, &p, std::move(before), context.onChanged));
                        reverted = true;
                    }
                }
                const bool canApply = overridden && context.applyToPrefab &&
                    p.type != Reflection::PropertyType::ObjectRef;
                if (ImGui::MenuItem("この値をプレハブへ適用", nullptr, false, canApply)) {
                    context.applyToPrefab(p);
                }
                ImGui::EndPopup();
            }
            return reverted;
        }
    }

    bool InspectorRenderer::IsEnabled()
    {
        return Reflection::IsEnabled();
    }

    namespace
    {
        /// @brief 直前の項目にカーソルが乗っていれば、プロパティの説明を出す
        void ShowPropertyTooltip(const Reflection::PropertyDescriptor& p, bool hovered)
        {
            if (hovered && p.tooltip && p.tooltip[0] != '\0') {
                ImGui::SetTooltip("%s", p.tooltip);
            }
        }
    }

    bool InspectorRenderer::Draw(const Reflection::TypeDescriptor& type, void* instance,
                                 const DrawContext& context)
    {
        if (!instance) {
            return false;
        }

        const char* fallbackLabel = type.displayName && type.displayName[0] ? type.displayName : type.name;
        const std::string ownerLabel = context.label.empty() ? fallbackLabel : context.label;
        const bool ownContextMenu = context.prefabParameters != nullptr;

        bool changed = false;
        for (const auto& p : type.properties) {
            if (!p.IsVisible() || !p.IsValid()) {
                continue;
            }

            // 値を自分で持たない型もあるので、記述子の getter で控えに読み出してから編集する。
            // 編集されたら setter で書き戻す
            Reflection::PropertyValue current;
            current.LoadFrom(p, instance);
            void* value = current.Data(p.type);
            if (!value) {
                continue;
            }

            if (!p.IsEditable()) {
                DrawReadOnly(p, value);
                ShowPropertyTooltip(p, ImGui::IsItemHovered());
                continue;
            }

            // 参照（ObjectRef / AssetRef）は選んだその場で履歴へ積む
            if (p.type == Reflection::PropertyType::ObjectRef ||
                p.type == Reflection::PropertyType::AssetRef) {
                Reflection::PropertyValue before;
                before.CopyFrom(p.type, value);

                ImGui::PushID(p.name.c_str());
                const bool retargeted = (p.type == Reflection::PropertyType::ObjectRef)
                    ? DrawObjectRef(p, *static_cast<Reflection::ObjectRefValue*>(value), context.objects)
                    : DrawAssetRef(p, *static_cast<Reflection::AssetRefValue*>(value));
                const bool hovered = ImGui::IsItemHovered();
                changed |= DrawPrefabOverride(p, instance, context, ownerLabel);
                ImGui::PopID();
                ShowPropertyTooltip(p, hovered);

                if (retargeted) {
                    current.StoreTo(p, instance);
                    changed = true;
                    if (context.onChanged) { context.onChanged(p); }
                    Editor::EditorCommandStack::Get().Push(
                        std::make_unique<PropertyEditCommand>(
                            ownerLabel + " の " + p.displayName,
                            context.owner, instance, &p, std::move(before), context.onChanged));
                }
                continue;
            }

            // 配列は要素の欄とボタンを 1 つの項目にまとめる。並びの変更はその場で、値の編集は離したときに履歴へ積む
            if (p.type == Reflection::PropertyType::Array) {
                static Reflection::PropertyValue arraySnapshot;
                Reflection::PropertyValue before;
                before.CopyFrom(p.type, value);

                ImGui::PushID(p.name.c_str());
                ImGui::BeginGroup();
                bool structureChanged = false;
                const bool edited = DrawArray(p, *static_cast<Reflection::ArrayValue*>(value),
                    ownContextMenu, structureChanged);
                ImGui::EndGroup();
                const bool hovered = ImGui::IsItemHovered();

                if (edited) {
                    current.StoreTo(p, instance);
                    changed = true;
                    if (context.onChanged) { context.onChanged(p); }
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && arraySnapshot.IsValid()) {
                    if (!arraySnapshot.Equals(p.type, before.Data(p.type))) {
                        Editor::EditorCommandStack::Get().Push(
                            std::make_unique<PropertyEditCommand>(
                                ownerLabel + " の " + p.displayName,
                                context.owner, instance, &p, arraySnapshot, context.onChanged));
                    }
                    arraySnapshot.Reset();
                }
                if (ImGui::IsItemActivated()) {
                    arraySnapshot = before;
                }
                if (structureChanged) {
                    Editor::EditorCommandStack::Get().Push(
                        std::make_unique<PropertyEditCommand>(
                            ownerLabel + " の " + p.displayName,
                            context.owner, instance, &p, std::move(before), context.onChanged));
                    arraySnapshot.Reset();
                }
                changed |= DrawPrefabOverride(p, instance, context, ownerLabel);
                ImGui::PopID();
                ShowPropertyTooltip(p, hovered);
                continue;
            }

            // 描く前の値を控え、掴んだフレームでそれを編集前の値として持ち、離した瞬間に 1 件だけ履歴へ積む。
            // ImGui のアクティブ項目は同時に 1 つなので控えも 1 つでよい
            static Reflection::PropertyValue editSnapshot;
            Reflection::PropertyValue before;
            before.CopyFrom(p.type, value);

            ImGui::PushID(p.name.c_str());
            const bool edited = DrawWidget(p, value, ownContextMenu);
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated()) {
                editSnapshot = std::move(before);
            }
            if (edited) {
                current.StoreTo(p, instance);
                changed = true;
                if (context.onChanged) { context.onChanged(p); }
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && editSnapshot.IsValid()) {
                // 掴んだだけで値が変わっていないなら履歴を汚さない
                if (!editSnapshot.Equals(p.type, value)) {
                    Editor::EditorCommandStack::Get().Push(
                        std::make_unique<PropertyEditCommand>(
                            ownerLabel + " の " + p.displayName,
                            context.owner, instance, &p, editSnapshot, context.onChanged));
                }
                editSnapshot.Reset();
            }
            changed |= DrawPrefabOverride(p, instance, context, ownerLabel);
            ImGui::PopID();
            ShowPropertyTooltip(p, hovered);
        }
        return changed;
    }
}

#endif // USE_IMGUI
