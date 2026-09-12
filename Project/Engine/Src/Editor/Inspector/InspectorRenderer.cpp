#include "pch.h"
#include "Editor/Inspector/InspectorRenderer.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include "Reflection/ReflectionToggle.h"
#include "Reflection/TypeDescriptor.h"

#include <cstring>
#include <vector>

namespace CoreEngine
{
    namespace
    {
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
                                 const EditCommitted& onCommitted)
    {
        if (!instance) {
            return false;
        }

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

            // ドラッグ開始時の値を控え、離した瞬間に 1 件だけ履歴へ積む
            static thread_local std::vector<uint8_t> editSnapshot;

            ImGui::PushID(p.name.c_str());
            const bool edited = DrawWidget(p, value);
            if (ImGui::IsItemActivated() && onCommitted) {
                const size_t size = Reflection::SizeOfPropertyType(p.type);
                editSnapshot.assign(size, 0);
                std::memcpy(editSnapshot.data(), value, size);
            }
            if (edited) {
                changed = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onCommitted && !editSnapshot.empty()) {
                onCommitted(p, editSnapshot.data());
                editSnapshot.clear();
            }
            ImGui::PopID();
        }
        return changed;
    }
}

#endif // USE_IMGUI
