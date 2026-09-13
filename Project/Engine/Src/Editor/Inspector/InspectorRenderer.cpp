#include "pch.h"
#include "Editor/Inspector/InspectorRenderer.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Reflection/PropertyValue.h"
#include "Reflection/ReflectionToggle.h"
#include "Reflection/TypeDescriptor.h"

#include <cstdint>
#include <cstring>
#include <memory>
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
            }
        }

        /// @brief 型に応じたウィジェットを描く
        bool DrawWidget(const Reflection::PropertyDescriptor& p, void* value)
        {
            using Reflection::PropertyType;
            const auto& r = p.range;
            const float min = r.valid ? r.min : 0.0f;
            const float max = r.valid ? r.max : 0.0f;

            switch (p.type) {
            case PropertyType::Bool:
                return ImGui::Checkbox(p.displayName, static_cast<bool*>(value));
            case PropertyType::Int: {
                const int iMin = r.valid ? static_cast<int>(r.min) : 0;
                const int iMax = r.valid ? static_cast<int>(r.max) : 0;
                return ImGui::DragInt(p.displayName, static_cast<int*>(value),
                    ResolveSpeed(r, 1.0f), iMin, iMax);
            }
            case PropertyType::Float:
                return ImGui::DragFloat(p.displayName, static_cast<float*>(value),
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector2:
                return ImGui::DragFloat2(p.displayName, &static_cast<Vector2*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector3:
                return ImGui::DragFloat3(p.displayName, &static_cast<Vector3*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Vector4:
                return ImGui::DragFloat4(p.displayName, &static_cast<Vector4*>(value)->x,
                    ResolveSpeed(r, 0.01f), min, max);
            case PropertyType::Color:
                return ImGui::ColorEdit4(p.displayName, &static_cast<Vector4*>(value)->x);
            case PropertyType::String: {
                auto* text = static_cast<std::string*>(value);
                char buffer[256]{};
                const size_t length = (std::min)(text->size(), sizeof(buffer) - 1);
                std::memcpy(buffer, text->data(), length);
                if (ImGui::InputText(p.displayName, buffer, sizeof(buffer))) {
                    *text = buffer;
                    return true;
                }
                return false;
            }
            case PropertyType::ObjectRef:
                return false;
            }
            return false;
        }

        /// @brief ObjectRef の値を、指定したオブジェクトの指せるコンポーネントへ向け直す
        /// @return 繋ぎ先が変わったら true
        bool Retarget(Reflection::ObjectRefValue& ref, const GameObject& object,
                      Reflection::PropertyDescriptor::ComponentFilter accepts)
        {
            const IComponent* component = FindReferencedComponent(object, accepts, {});
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
                (!target || !FindReferencedComponent(*target, p.acceptsComponent, ref.componentType));

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
                                edited = Retarget(ref, *dropped, p.acceptsComponent);
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
                        !FindReferencedComponent(*object, p.acceptsComponent, {})) {
                        continue;
                    }

                    const bool selected = object->GetObjectId() == ref.objectId;
                    ImGui::PushID(object.get());
                    if (ImGui::Selectable(object->GetDisplayName(), selected)) {
                        edited = Retarget(ref, *object, p.acceptsComponent) || edited;
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
    }

    bool InspectorRenderer::IsEnabled()
    {
        return Reflection::IsEnabled();
    }

    bool InspectorRenderer::Draw(const Reflection::TypeDescriptor& type, void* instance,
                                 const DrawContext& context)
    {
        if (!instance) {
            return false;
        }

        const char* fallbackLabel = type.displayName && type.displayName[0] ? type.displayName : type.name;
        const std::string ownerLabel = context.label.empty() ? fallbackLabel : context.label;

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
                continue;
            }

            // 繋ぎ先の選択は 1 回で確定するので、選んだその場で履歴へ積む
            if (p.type == Reflection::PropertyType::ObjectRef) {
                Reflection::PropertyValue before;
                before.CopyFrom(p.type, value);

                ImGui::PushID(p.name.c_str());
                const bool retargeted = DrawObjectRef(
                    p, *static_cast<Reflection::ObjectRefValue*>(value), context.objects);
                ImGui::PopID();

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

            // ドラッグ開始時の値を控え、離した瞬間に 1 件だけ履歴へ積む。
            // ImGui のアクティブ項目は同時に 1 つなので控えも 1 つでよい
            static Reflection::PropertyValue editSnapshot;

            ImGui::PushID(p.name.c_str());
            const bool edited = DrawWidget(p, value);
            if (ImGui::IsItemActivated()) {
                editSnapshot.CopyFrom(p.type, value);
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
            ImGui::PopID();
        }
        return changed;
    }
}

#endif // USE_IMGUI
