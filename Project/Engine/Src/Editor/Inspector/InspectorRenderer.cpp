#include "pch.h"
#include "Editor/Inspector/InspectorRenderer.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Reflection/PropertyValue.h"
#include "Reflection/ReflectionToggle.h"
#include "Reflection/TypeDescriptor.h"

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
                if (!hasAfter_) {
                    if (const void* current = CurrentValue()) {
                        after_.CopyFrom(property_->type, current);
                        hasAfter_ = true;
                    }
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
            void* CurrentValue() const
            {
                return (property_ && instance_) ? property_->ValuePtr(instance_) : nullptr;
            }

            void Apply(const Reflection::PropertyValue& value)
            {
                void* destination = CurrentValue();
                if (!destination || !value.ApplyTo(property_->type, destination)) {
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
            }
            return false;
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
            if (!p.IsVisible()) {
                continue;
            }
            void* value = p.ValuePtr(instance);
            if (!value) {
                continue;
            }

            if (!p.IsEditable()) {
                DrawReadOnly(p, value);
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
