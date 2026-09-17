#include "pch.h"
#include "Editor/Inspector/InspectorRenderer.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/InspectorLayout.h"
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
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 1 つのプロパティの右クリックのメニュー
        constexpr const char* kPropertyMenuId = "##propertyMenu";

        /// @brief 記述子経由で編集した 1 プロパティを元に戻すコマンド
        /// @details 文脈がコンポーネントを引き直す関数を持っていれば、適用のたびに引き直し、
        ///          プロパティは名前で探す。持っていなければ積んだときの実体へ書く。
        class PropertyEditCommand final : public Editor::IEditorCommand
        {
        public:
            PropertyEditCommand(std::string label, const InspectorRenderer::DrawContext& context, void* instance,
                                const Reflection::PropertyDescriptor& property, Reflection::PropertyValue before)
                : label_(std::move(label)), owner_(context.owner), instance_(instance), property_(&property),
                  propertyName_(property.name), propertyType_(property.type), before_(std::move(before)),
                  onChanged_(context.onChanged), resolveComponent_(context.resolveComponent) {}

            void Undo() override
            {
                const std::optional<Target> target = ResolveTarget();
                if (!target) {
                    return;
                }
                // 積んだ時点では編集後の値が確定していないので、最初の Undo で控える
                if (!hasAfter_) {
                    after_.LoadFrom(*target->property, target->instance);
                    hasAfter_ = after_.IsValid();
                }
                Apply(*target, before_);
            }

            void Redo() override
            {
                const std::optional<Target> target = ResolveTarget();
                if (target && hasAfter_) {
                    Apply(*target, after_);
                }
            }

            std::string GetLabel() const override { return label_; }

            bool References(const void* target) const override
            {
                return !resolveComponent_ && target != nullptr && (target == owner_ || target == instance_);
            }

            bool SurvivesSceneReload() const override { return static_cast<bool>(resolveComponent_); }

        private:
            /// @brief 書き込む先
            struct Target
            {
                void* instance = nullptr;
                const Reflection::PropertyDescriptor* property = nullptr;
                IComponent* component = nullptr; ///< 引き直したコンポーネント（引き直さないなら nullptr）
            };

            std::optional<Target> ResolveTarget() const
            {
                if (!resolveComponent_) {
                    if (!instance_ || !property_) {
                        return std::nullopt;
                    }
                    return Target{ instance_, property_, nullptr };
                }

                IComponent* const component = resolveComponent_();
                const Reflection::TypeDescriptor* const type = component ? component->GetTypeDescriptor() : nullptr;
                const Reflection::PropertyDescriptor* const property =
                    type ? type->Find(propertyName_.c_str()) : nullptr;
                void* const instance = component ? component->GetReflectionInstance() : nullptr;
                if (!property || property->type != propertyType_ || !instance) {
                    return std::nullopt;
                }
                return Target{ instance, property, component };
            }

            void Apply(const Target& target, const Reflection::PropertyValue& value)
            {
                if (!value.StoreTo(*target.property, target.instance)) {
                    return;
                }
                if (target.component) {
                    target.component->OnPropertyChanged(*target.property);
                } else if (onChanged_) {
                    onChanged_(*target.property);
                }
            }

            std::string label_;
            const void* owner_ = nullptr;
            void*       instance_ = nullptr;
            const Reflection::PropertyDescriptor* property_ = nullptr;
            std::string propertyName_;
            Reflection::PropertyType propertyType_{};
            Reflection::PropertyValue before_;
            Reflection::PropertyValue after_;
            bool hasAfter_ = false;
            std::function<void(const Reflection::PropertyDescriptor&)> onChanged_;
            std::function<IComponent*()> resolveComponent_;
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

        /// @brief 範囲の決まった数値の欄を、値の位置まで塗った下地の上に描く
        /// @param fraction 範囲の中での値の位置（0〜1 の外は端に丸める）
        /// @param drawField 欄を描いて、値が変わったら true を返す関数
        template <class DrawField>
        bool DrawWithFill(float fraction, DrawField&& drawField)
        {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + ImGui::CalcItemWidth(), min.y + ImGui::GetFrameHeight());
            const float rounding = ImGui::GetStyle().FrameRounding;
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
            const float filled = min.x + (max.x - min.x) * std::clamp(fraction, 0.0f, 1.0f);
            if (filled > min.x) {
                drawList->AddRectFilled(min, ImVec2(filled, max.y), ImGui::GetColorU32(Theme::kAccentMuted), rounding);
            }

            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::kTransparent);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::WithAlpha(Theme::kHover, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::WithAlpha(Theme::kActive, 0.35f));
            const bool changed = drawField();
            ImGui::PopStyleColor(3);
            return changed;
        }

        /// @brief 成分の印と文字の色（X 赤・Y 緑・Z 青。W は淡色）
        const ImVec4& AxisColorOf(int axis)
        {
            switch (axis) {
            case 0:  return Theme::kAxisX;
            case 1:  return Theme::kAxisY;
            case 2:  return Theme::kAxisZ;
            default: return Theme::kTextMute;
            }
        }

        /// @brief ベクトルの欄（成分ごとの欄の左端に、軸の色の印と文字を添えて並べる）
        bool DragVector(const char* id, float* values, int count, float speed, float min, float max)
        {
            static constexpr const char* kAxes[] = { "X", "Y", "Z", "W" };
            constexpr float kAxisMarkWidth = 3.0f;
            const ImGuiStyle& style = ImGui::GetStyle();
            const float spacing = style.ItemInnerSpacing.x;
            const float fieldWidth = (std::max)(1.0f,
                (ImGui::CalcItemWidth() - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));

            bool changed = false;
            ImGui::PushID(id);
            ImGui::BeginGroup();
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                if (i > 0) {
                    ImGui::SameLine(0.0f, spacing);
                }
                ImGui::SetNextItemWidth(fieldWidth);
                changed = ImGui::DragFloat("##axis", &values[i], speed, min, max, "%.3f") || changed;

                const ImVec2 fieldMin = ImGui::GetItemRectMin();
                const ImVec2 fieldMax = ImGui::GetItemRectMax();
                const ImU32 axisColor = ImGui::GetColorU32(AxisColorOf(i));
                drawList->AddRectFilled(fieldMin, ImVec2(fieldMin.x + kAxisMarkWidth, fieldMax.y), axisColor,
                    style.FrameRounding, ImDrawFlags_RoundCornersLeft);
                drawList->AddText(
                    ImVec2(fieldMin.x + kAxisMarkWidth + style.FramePadding.x * 0.5f, fieldMin.y + style.FramePadding.y),
                    axisColor, kAxes[i]);
                ImGui::PopID();
            }
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        /// @brief 表示用の倍率を掛けた値で欄を描き、動いた成分だけを書き戻す
        /// @param drawField 倍率を掛けた値の配列を受け取って欄を描き、動いたら true を返す関数
        template <class DrawField>
        bool DrawScaled(float* values, int count, float scale, DrawField&& drawField)
        {
            constexpr int kMaxComponents = 4;
            float shown[kMaxComponents]{};
            for (int i = 0; i < count; ++i) {
                shown[i] = values[i] * scale;
            }
            if (!drawField(shown)) {
                return false;
            }
            for (int i = 0; i < count; ++i) {
                if (shown[i] != values[i] * scale) {
                    values[i] = shown[i] / scale;
                }
            }
            return true;
        }

        /// @brief 表示用の倍率を掛けてベクトルの欄を描く（範囲と速度にも同じ倍率を掛ける）
        bool DragScaledVector(const char* id, float* values, int count, const Reflection::PropertyRange& r, float scale)
        {
            const float min = r.valid ? r.min * scale : 0.0f;
            const float max = r.valid ? r.max * scale : 0.0f;
            return DrawScaled(values, count, scale, [&](float* shown) {
                return DragVector(id, shown, count, ResolveSpeed(r, 0.01f) * scale, min, max);
                });
        }

        /// @brief std::string をそのまま編集する入力欄（長さの上限なし）
        /// @param multiline true なら複数行で編集する
        bool InputString(const char* id, std::string* text, bool multiline)
        {
            const ImGuiInputTextCallback resize = [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* const target = static_cast<std::string*>(data->UserData);
                    target->resize(static_cast<std::size_t>(data->BufTextLen));
                    data->Buf = target->data();
                }
                return 0;
            };
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
            if (multiline) {
                const ImVec2 size(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f);
                return ImGui::InputTextMultiline(id, text->data(), text->capacity() + 1, size, flags, resize, text);
            }
            return ImGui::InputText(id, text->data(), text->capacity() + 1, flags, resize, text);
        }

        /// @brief 文字列を候補の一覧から選ぶ欄（一覧の上の入力欄に名前を打って Enter でも決まる）
        /// @param instance 候補を記述子に尋ねるときに渡す持ち主
        /// @return 別の値を選んだら true
        bool DrawStringChoices(const Reflection::PropertyDescriptor& p, std::string& value, const void* instance)
        {
            if (!ImGui::BeginCombo("##value", value.empty() ? "（なし）" : value.c_str())) {
                return false;
            }

            bool changed = false;
            static std::string typed;
            if (ImGui::IsWindowAppearing()) {
                typed = value;
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##typed", "名前を入力して Enter", typed.data(), typed.capacity() + 1,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                    [](ImGuiInputTextCallbackData* data) -> int {
                        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                            auto* const target = static_cast<std::string*>(data->UserData);
                            target->resize(static_cast<std::size_t>(data->BufTextLen));
                            data->Buf = target->data();
                        }
                        return 0;
                    }, &typed)) {
                if (!typed.empty() && typed != value) {
                    value = typed;
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();

            for (const std::string& choice : p.stringChoices(instance)) {
                const bool selected = choice == value;
                if (ImGui::Selectable(choice.c_str(), selected) && !selected) {
                    value = choice;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
            return changed;
        }

        /// @brief 整数を名前の一覧から選ぶ欄
        /// @return 別の値を選んだら true
        bool DrawEnumCombo(const char* id, int* value, const char* const* names, int count)
        {
            const char* preview = (*value >= 0 && *value < count) ? names[*value] : "";
            bool changed = false;
            if (ImGui::BeginCombo(id, preview)) {
                for (int i = 0; i < count; ++i) {
                    const bool selected = i == *value;
                    if (ImGui::Selectable(names[i], selected) && !selected) {
                        *value = i;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        /// @brief 色の欄（幅いっぱいのスウォッチを押すとピッカーが開く）
        /// @param hasAlpha false なら不透明度を出さない（`rgba` の 4 つ目は読み書きしない）
        /// @note ピッカーを同じグループの中で描くので、ピッカーを掴んだ・離したが呼ぶ側の直前の項目として見える。
        bool DrawColorSwatch(const char* id, float* rgba, bool hasAlpha)
        {
            constexpr const char* kPickerId = "##colorPicker";
            const ImVec2 size((std::max)(1.0f, ImGui::CalcItemWidth()), ImGui::GetFrameHeight());
            const ImGuiColorEditFlags swatchFlags = hasAlpha
                ? ImGuiColorEditFlags_AlphaPreviewHalf
                : ImGuiColorEditFlags_NoAlpha;
            const ImGuiColorEditFlags pickerFlags = hasAlpha
                ? (ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)
                : ImGuiColorEditFlags_NoAlpha;

            bool changed = false;
            ImGui::PushID(id);
            ImGui::BeginGroup();
            const ImVec4 color(rgba[0], rgba[1], rgba[2], hasAlpha ? rgba[3] : 1.0f);
            if (ImGui::ColorButton("##swatch", color, swatchFlags, size)) {
                ImGui::OpenPopup(kPickerId);
            }
            if (ImGui::BeginPopup(kPickerId)) {
                changed = ImGui::ColorPicker4("##picker", rgba, pickerFlags);
                ImGui::EndPopup();
            }
            ImGui::EndGroup();
            ImGui::PopID();
            return changed;
        }

        /// @brief 値を表示用の文字列にする（編集できない欄に使う）
        std::string FormatValue(const Reflection::PropertyDescriptor& p, const void* value)
        {
            using Reflection::PropertyType;
            const float s = p.displayScale;
            switch (p.type) {
            case PropertyType::Bool:
                return *static_cast<const bool*>(value) ? "true" : "false";
            case PropertyType::Int: {
                const int number = *static_cast<const int*>(value);
                if (p.enumNames && number >= 0 && number < p.enumCount) {
                    return p.enumNames[number];
                }
                return std::to_string(number);
            }
            case PropertyType::Float:
                return std::format("{:.3f}", *static_cast<const float*>(value) * s);
            case PropertyType::Vector2: {
                const auto& v = *static_cast<const Vector2*>(value);
                return std::format("{:.3f}, {:.3f}", v.x * s, v.y * s);
            }
            case PropertyType::Vector3: {
                const auto& v = *static_cast<const Vector3*>(value);
                return std::format("{:.3f}, {:.3f}, {:.3f}", v.x * s, v.y * s, v.z * s);
            }
            case PropertyType::Vector4:
            case PropertyType::Color: {
                const auto& v = *static_cast<const Vector4*>(value);
                return std::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}", v.x, v.y, v.z, v.w);
            }
            case PropertyType::String:
                return *static_cast<const std::string*>(value);
            case PropertyType::ObjectRef: {
                const auto& ref = *static_cast<const Reflection::ObjectRefValue*>(value);
                return ref.objectId.IsValid() ? "◆ #" + ref.objectId.ToString() : std::string("（なし）");
            }
            case PropertyType::AssetRef: {
                const auto& ref = *static_cast<const Reflection::AssetRefValue*>(value);
                return ref.path.empty() ? std::string("（なし）") : ref.path;
            }
            case PropertyType::Array:
                return std::format("{} 個", static_cast<const Reflection::ArrayValue*>(value)->elements.size());
            }
            return {};
        }

        /// @brief 編集できないプロパティを、沈んだ欄に淡い文字で描く
        /// @return 行にカーソルが乗っていれば true
        bool DrawReadOnly(const Reflection::PropertyDescriptor& p, const void* value)
        {
            bool hovered = InspectorLayout::BeginRow(p.displayName, Theme::kTextDim);

            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 size((std::max)(1.0f, ImGui::CalcItemWidth()), ImGui::GetFrameHeight());
            const ImVec2 max(min.x + size.x, min.y + size.y);
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kDeepest), ImGui::GetStyle().FrameRounding);

            const std::string text = FormatValue(p, value);
            const ImVec2 padding = ImGui::GetStyle().FramePadding;
            drawList->PushClipRect(min, max, true);
            drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y),
                ImGui::GetColorU32(Theme::kTextMute), text.c_str());
            drawList->PopClipRect();

            ImGui::Dummy(size);
            hovered = ImGui::IsItemHovered() || hovered;
            return hovered;
        }

        /// @brief 型に応じたウィジェットを 1 つ描く
        /// @param p 範囲・表示の倍率・値の名前・不透明度の扱いを読む記述子
        /// @param type 描く値の型（配列の要素なら `p.elementType`）
        bool DrawValueWidget(const Reflection::PropertyDescriptor& p, Reflection::PropertyType type,
                             const char* label, void* value)
        {
            using Reflection::PropertyType;
            const Reflection::PropertyRange& r = p.range;
            const bool ranged = r.valid && r.max > r.min;
            const float scale = p.displayScale > 0.0f ? p.displayScale : 1.0f;

            switch (type) {
            case PropertyType::Bool:
                return ImGui::Checkbox(label, static_cast<bool*>(value));
            case PropertyType::Int: {
                auto* const number = static_cast<int*>(value);
                if (p.enumNames && p.enumCount > 0) {
                    return DrawEnumCombo(label, number, p.enumNames, p.enumCount);
                }
                const int iMin = r.valid ? static_cast<int>(r.min) : 0;
                const int iMax = r.valid ? static_cast<int>(r.max) : 0;
                const auto drawField = [&] {
                    return ImGui::DragInt(label, number, ResolveSpeed(r, 1.0f), iMin, iMax);
                };
                return ranged
                    ? DrawWithFill((static_cast<float>(*number) - r.min) / (r.max - r.min), drawField)
                    : drawField();
            }
            case PropertyType::Float: {
                auto* const number = static_cast<float*>(value);
                const float min = r.valid ? r.min * scale : 0.0f;
                const float max = r.valid ? r.max * scale : 0.0f;
                const auto drawField = [&] {
                    return DrawScaled(number, 1, scale, [&](float* shown) {
                        return ImGui::DragFloat(label, shown, ResolveSpeed(r, 0.01f) * scale, min, max);
                        });
                };
                return ranged ? DrawWithFill((*number - r.min) / (r.max - r.min), drawField) : drawField();
            }
            case PropertyType::Vector2:
                return DragScaledVector(label, &static_cast<Vector2*>(value)->x, 2, r, scale);
            case PropertyType::Vector3:
                return DragScaledVector(label, &static_cast<Vector3*>(value)->x, 3, r, scale);
            case PropertyType::Vector4:
                return DragScaledVector(label, &static_cast<Vector4*>(value)->x, 4, r, scale);
            case PropertyType::Color:
                return DrawColorSwatch(label, &static_cast<Vector4*>(value)->x,
                    !Reflection::HasFlag(p.flags, Reflection::PropertyFlags::NoAlpha));
            case PropertyType::String:
                return InputString(label, static_cast<std::string*>(value),
                    Reflection::HasFlag(p.flags, Reflection::PropertyFlags::Multiline));
            case PropertyType::ObjectRef:
            case PropertyType::AssetRef:
            case PropertyType::Array:
                return false;
            }
            return false;
        }

        /// @brief 配列の欄を描く（要素ごとの欄と、消す・並べ替える・足すボタン）
        /// @param labelColor 見出しの文字の色
        /// @param structureChanged 要素を足す・消す・並べ替えたら true にする
        /// @return 要素の値か並びが変わったら true
        bool DrawArray(const Reflection::PropertyDescriptor& p, Reflection::ArrayValue& array,
                       const ImVec4& labelColor, bool& structureChanged)
        {
            std::vector<Reflection::ArrayValue::Element>& elements = array.elements;
            ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
            const bool open = ImGui::TreeNodeEx("##array",
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen,
                "%s（%d 個）", p.displayName, static_cast<int>(elements.size()));
            ImGui::PopStyleColor();
            // 見出しを右クリックしてもプロパティのメニューを開く（ID をずらさないよう、字下げの前に行う）
            ImGui::OpenPopupOnItemClick(kPropertyMenuId, ImGuiPopupFlags_MouseButtonRight);
            if (!open) {
                return false;
            }
            ImGui::TreePush("##array");

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
                    edited = DrawValueWidget(p, p.elementType, "##value", data) || edited;
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

        /// @brief コンボの枠の範囲（矢印を除く）を、描く前に控える
        struct ComboPreviewArea
        {
            ImDrawList* drawList = nullptr;
            ImVec2 min;
            ImVec2 max;
        };

        ComboPreviewArea CaptureComboPreviewArea()
        {
            ComboPreviewArea area;
            area.drawList = ImGui::GetWindowDrawList();
            area.min = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetFrameHeight();
            area.max = ImVec2(area.min.x + ImGui::CalcItemWidth() - height, area.min.y + height);
            return area;
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

            std::string name = "（なし）";
            std::string idText;
            const char* icon = nullptr;
            if (missing) {
                name = "（見つかりません " + ref.objectId.ToString() + "）";
            } else if (target) {
                name = target->GetDisplayName();
                idText = "#" + InspectorLayout::ShortId(ref.objectId.ToString(), 4);
                icon = "◆";
            }

            // 中身は枠の上に重ねて描く。開くとポップアップが今の窓になるので、描く先を先に控える
            const ComboPreviewArea area = CaptureComboPreviewArea();
            const bool open = ImGui::BeginCombo("##value", "");
            InspectorLayout::DrawReferencePreview(area.drawList, area.min, area.max,
                icon, name.c_str(), idText.c_str(), missing);

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
        /// @param instance 何も指していないときの表示を記述子に尋ねるときに渡す持ち主
        /// @return 指す先が変わったら true
        bool DrawAssetRef(const Reflection::PropertyDescriptor& p, Reflection::AssetRefValue& ref,
                          const void* instance)
        {
            const AssetInfo* target = ResolveAssetRef(ref);
            const bool isSet = !ref.guid.empty() || !ref.path.empty();
            const bool invalid = isSet && (!target || target->type != p.assetType);

            std::string name = "（なし）";
            std::string idText;
            const char* icon = nullptr;
            if (!isSet && p.emptyText) {
                if (std::string text = p.emptyText(instance); !text.empty()) {
                    name = std::move(text);
                    icon = InspectorLayout::AssetGlyph(p.assetType);
                }
            } else if (isSet && !target) {
                name = "（見つかりません " + (ref.path.empty() ? ref.guid : ref.path) + "）";
            } else if (invalid) {
                name = "（種類が違います " + target->fileName + "）";
            } else if (target) {
                name = target->fileName;
                idText = InspectorLayout::ShortId(ref.guid);
                icon = InspectorLayout::AssetGlyph(target->type);
            }

            // 中身は枠の上に重ねて描く。開くとポップアップが今の窓になるので、描く先を先に控える
            const ComboPreviewArea area = CaptureComboPreviewArea();
            const bool open = ImGui::BeginCombo("##value", "");
            InspectorLayout::DrawReferencePreview(area.drawList, area.min, area.max,
                icon, name.c_str(), idText.c_str(), invalid);

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

        /// @brief 1 つのプロパティの、既定値とプレハブとの見比べ
        struct PropertyState
        {
            const json* defaultValue = nullptr; ///< 新しく作ったときの値（分からなければ nullptr）
            bool modified = false;              ///< 既定値と違うか
            const json* prefabValue = nullptr;  ///< プレハブでの値（プレハブに無ければ nullptr）
            bool overridden = false;            ///< プレハブと違うか
        };

        /// @brief 今の値を既定値・プレハブの値と見比べる
        PropertyState InspectState(const Reflection::PropertyDescriptor& p, const void* instance,
                                   const InspectorRenderer::DrawContext& context)
        {
            PropertyState state;
            if (!p.IsSaved() || (!context.defaultParameters && !context.prefabParameters)) {
                return state;
            }
            const json current = Reflection::PropertySerializer::PropertyToJson(p, instance);

            if (context.defaultParameters) {
                const auto found = context.defaultParameters->find(p.name);
                if (found != context.defaultParameters->end()) {
                    state.defaultValue = &*found;
                    state.modified = !PrefabSystem::SameValue(*found, current);
                }
            }
            if (context.prefabParameters) {
                const auto found = context.prefabParameters->find(p.name);
                if (found != context.prefabParameters->end()) {
                    state.prefabValue = &*found;
                    state.overridden = !PrefabSystem::SameValue(*found, current);
                } else {
                    state.overridden = true;
                }
            }
            return state;
        }

        /// @brief ラベルの色（既定値から変えていれば橙）
        const ImVec4& LabelColorOf(const PropertyState& state)
        {
            return state.modified ? Theme::kWarm : Theme::kTextDim;
        }

        /// @brief プレハブと違う値の行の左端（窓の余白）に線を引く
        void DrawOverrideMark(const PropertyState& state, const ImVec2& rowStart)
        {
            if (!state.overridden) {
                return;
            }
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(rowStart.x - 4.0f, rowStart.y), ImVec2(rowStart.x - 1.0f, ImGui::GetItemRectMax().y),
                ImGui::GetColorU32(Theme::kAccent));
        }

        /// @brief 保存形の値をプロパティへ書き込み、元に戻すコマンドを作る
        /// @return 書き込めなければ nullptr
        std::unique_ptr<Editor::IEditorCommand> AssignFromJson(const Reflection::PropertyDescriptor& p,
            void* instance, const json& node, const InspectorRenderer::DrawContext& context, std::string label)
        {
            Reflection::PropertyValue before;
            before.LoadFrom(p, instance);
            if (!Reflection::PropertySerializer::JsonToProperty(p, instance, node)) {
                return nullptr;
            }
            if (context.onChanged) { context.onChanged(p); }
            return std::make_unique<PropertyEditCommand>(std::move(label), context, instance, p, std::move(before));
        }

        /// @brief 値を書き込み、書き込めたら履歴へ積む
        /// @return 書き込めたら true
        bool AssignAndPush(const Reflection::PropertyDescriptor& p, void* instance, const json& node,
                           const InspectorRenderer::DrawContext& context, std::string label)
        {
            std::unique_ptr<Editor::IEditorCommand> command = AssignFromJson(p, instance, node, context, std::move(label));
            if (!command) {
                return false;
            }
            Editor::EditorCommandStack::Get().Push(std::move(command));
            return true;
        }

        /// @brief プロパティの右クリックのメニュー（既定値へ戻す・プレハブの値に戻す・プレハブへ適用）
        /// @return 値を書き換えたら true
        bool DrawPropertyMenu(const Reflection::PropertyDescriptor& p, void* instance, const PropertyState& state,
                              const InspectorRenderer::DrawContext& context, const std::string& ownerLabel)
        {
            if (!ImGui::BeginPopup(kPropertyMenuId)) {
                return false;
            }

            bool changed = false;
            ImGui::TextDisabled("%s", p.displayName);
            ImGui::Separator();

            if (ImGui::MenuItem("既定値へ戻す", nullptr, false, state.modified && state.defaultValue)) {
                changed = AssignAndPush(p, instance, *state.defaultValue, context,
                    ownerLabel + " の " + p.displayName + " を既定値へ戻す");
            }

            if (context.prefabParameters) {
                ImGui::Separator();
                if (ImGui::MenuItem("プレハブの値に戻す", nullptr, false, state.overridden && state.prefabValue)) {
                    changed = AssignAndPush(p, instance, *state.prefabValue, context,
                        ownerLabel + " の " + p.displayName + " をプレハブの値に戻す") || changed;
                }
                const bool canApply = state.overridden && context.applyToPrefab &&
                    p.type != Reflection::PropertyType::ObjectRef;
                if (ImGui::MenuItem("この値をプレハブへ適用", nullptr, false, canApply)) {
                    context.applyToPrefab(p);
                }
            }

            ImGui::EndPopup();
            return changed;
        }

        /// @brief 一覧から選ぶ欄か（ObjectRef / AssetRef / 名前付きの整数 / 候補のある文字列）
        bool IsChosenFromList(const Reflection::PropertyDescriptor& p)
        {
            return p.type == Reflection::PropertyType::ObjectRef
                || p.type == Reflection::PropertyType::AssetRef
                || (p.type == Reflection::PropertyType::Int && p.enumNames != nullptr)
                || (p.type == Reflection::PropertyType::String && p.stringChoices != nullptr);
        }

        /// @brief 直前の項目か行にカーソルが乗っていれば、プロパティの説明を出す
        void ShowPropertyTooltip(const Reflection::PropertyDescriptor& p, bool hovered)
        {
            if (hovered && p.tooltip && p.tooltip[0] != '\0') {
                ImGui::SetTooltip("%s", p.tooltip);
            }
        }

        /// @brief 履歴に出す持ち主の名前
        std::string OwnerLabelOf(const Reflection::TypeDescriptor& type, const InspectorRenderer::DrawContext& context)
        {
            if (!context.label.empty()) {
                return context.label;
            }
            return type.displayName && type.displayName[0] ? type.displayName : type.name;
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

        const std::string ownerLabel = OwnerLabelOf(type, context);

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

            ImGui::PushID(p.name.c_str());
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();

            if (!p.IsEditable()) {
                ShowPropertyTooltip(p, DrawReadOnly(p, value));
                ImGui::PopID();
                continue;
            }

            const PropertyState state = InspectState(p, instance, context);

            // 一覧から選ぶ欄（ObjectRef / AssetRef / 名前付きの整数）は選んだその場で履歴へ積む
            if (IsChosenFromList(p)) {
                Reflection::PropertyValue before;
                before.CopyFrom(p.type, value);

                const bool labelHovered = InspectorLayout::BeginRow(p.displayName, LabelColorOf(state), kPropertyMenuId);
                bool retargeted = false;
                switch (p.type) {
                case Reflection::PropertyType::ObjectRef:
                    retargeted = DrawObjectRef(p, *static_cast<Reflection::ObjectRefValue*>(value), context.objects);
                    break;
                case Reflection::PropertyType::AssetRef:
                    retargeted = DrawAssetRef(p, *static_cast<Reflection::AssetRefValue*>(value), instance);
                    break;
                case Reflection::PropertyType::String:
                    retargeted = DrawStringChoices(p, *static_cast<std::string*>(value), instance);
                    break;
                default:
                    retargeted = DrawValueWidget(p, p.type, "##value", value);
                    break;
                }
                const bool hovered = ImGui::IsItemHovered() || labelHovered;
                ImGui::OpenPopupOnItemClick(kPropertyMenuId, ImGuiPopupFlags_MouseButtonRight);
                DrawOverrideMark(state, rowStart);

                if (retargeted) {
                    current.StoreTo(p, instance);
                    changed = true;
                    if (context.onChanged) { context.onChanged(p); }
                    Editor::EditorCommandStack::Get().Push(std::make_unique<PropertyEditCommand>(
                        ownerLabel + " の " + p.displayName, context, instance, p, std::move(before)));
                }
                changed = DrawPropertyMenu(p, instance, state, context, ownerLabel) || changed;
                ImGui::PopID();
                ShowPropertyTooltip(p, hovered);
                continue;
            }

            // 配列は要素の欄とボタンを 1 つの項目にまとめる。並びの変更はその場で、値の編集は離したときに履歴へ積む
            if (p.type == Reflection::PropertyType::Array) {
                static Reflection::PropertyValue arraySnapshot;
                Reflection::PropertyValue before;
                before.CopyFrom(p.type, value);

                ImGui::BeginGroup();
                bool structureChanged = false;
                const bool edited = DrawArray(p, *static_cast<Reflection::ArrayValue*>(value),
                    LabelColorOf(state), structureChanged);
                ImGui::EndGroup();
                const bool hovered = ImGui::IsItemHovered();

                if (edited) {
                    current.StoreTo(p, instance);
                    changed = true;
                    if (context.onChanged) { context.onChanged(p); }
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && arraySnapshot.IsValid()) {
                    if (!arraySnapshot.Equals(p.type, before.Data(p.type))) {
                        Editor::EditorCommandStack::Get().Push(std::make_unique<PropertyEditCommand>(
                            ownerLabel + " の " + p.displayName, context, instance, p, arraySnapshot));
                    }
                    arraySnapshot.Reset();
                }
                if (ImGui::IsItemActivated()) {
                    arraySnapshot = before;
                }
                if (structureChanged) {
                    Editor::EditorCommandStack::Get().Push(std::make_unique<PropertyEditCommand>(
                        ownerLabel + " の " + p.displayName, context, instance, p, std::move(before)));
                    arraySnapshot.Reset();
                }
                DrawOverrideMark(state, rowStart);
                changed = DrawPropertyMenu(p, instance, state, context, ownerLabel) || changed;
                ImGui::PopID();
                ShowPropertyTooltip(p, hovered);
                continue;
            }

            // 描く前の値を控え、掴んだフレームでそれを編集前の値として持ち、離した瞬間に 1 件だけ履歴へ積む。
            // ImGui のアクティブ項目は同時に 1 つなので控えも 1 つでよい
            static Reflection::PropertyValue editSnapshot;
            Reflection::PropertyValue before;
            before.CopyFrom(p.type, value);

            const bool labelHovered = InspectorLayout::BeginRow(p.displayName, LabelColorOf(state), kPropertyMenuId);
            const bool edited = DrawValueWidget(p, p.type, "##value", value);
            const bool hovered = ImGui::IsItemHovered() || labelHovered;
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
                    Editor::EditorCommandStack::Get().Push(std::make_unique<PropertyEditCommand>(
                        ownerLabel + " の " + p.displayName, context, instance, p, editSnapshot));
                }
                editSnapshot.Reset();
            }
            ImGui::OpenPopupOnItemClick(kPropertyMenuId, ImGuiPopupFlags_MouseButtonRight);
            DrawOverrideMark(state, rowStart);
            changed = DrawPropertyMenu(p, instance, state, context, ownerLabel) || changed;
            ImGui::PopID();
            ShowPropertyTooltip(p, hovered);
        }
        return changed;
    }

    bool InspectorRenderer::ResetToDefaults(const Reflection::TypeDescriptor& type, void* instance,
                                            const DrawContext& context)
    {
        if (!instance || !context.defaultParameters) {
            return false;
        }

        const std::string ownerLabel = OwnerLabelOf(type, context);
        auto composite = std::make_unique<Editor::CompositeCommand>(ownerLabel + " を既定値へ戻す");
        for (const auto& p : type.properties) {
            if (!p.IsVisible() || !p.IsValid() || !p.IsEditable()) {
                continue;
            }
            const PropertyState state = InspectState(p, instance, context);
            if (!state.modified || !state.defaultValue) {
                continue;
            }
            composite->Add(AssignFromJson(p, instance, *state.defaultValue, context,
                ownerLabel + " の " + p.displayName + " を既定値へ戻す"));
        }

        if (composite->IsEmpty()) {
            return false;
        }
        if (std::unique_ptr<Editor::IEditorCommand> single = composite->ExtractSingle()) {
            Editor::EditorCommandStack::Get().Push(std::move(single));
        } else {
            Editor::EditorCommandStack::Get().Push(std::move(composite));
        }
        return true;
    }
}

#endif // USE_IMGUI
