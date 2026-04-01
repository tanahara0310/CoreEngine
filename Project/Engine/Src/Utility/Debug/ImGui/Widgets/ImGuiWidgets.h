#pragma once

#ifdef USE_IMGUI

#include <imgui.h>
#include <imgui_internal.h>

namespace CoreEngine {
    namespace UI {
        namespace Widgets {

            /// @brief スライダー型トグルスイッチを描画する
            /// @param label ラベル文字列（"##" 始まりの場合は非表示）
            /// @param v     トグル状態へのポインタ
            /// @return クリックされた場合 true
            inline bool ToggleSwitch(const char* label, bool* v)
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const ImVec2 p = ImGui::GetCursorScreenPos();

                const float height = ImGui::GetFrameHeight();
                const float width = height * 1.8f;
                const float radius = height * 0.50f;
                const float knobPad = height * 0.12f;
                const float knobR = radius - knobPad;

                // アニメーション値を ImGui ストレージでフレームをまたいで保持
                const ImGuiID id = ImGui::GetID(label);
                ImGuiStorage* storage = ImGui::GetStateStorage();
                float anim_t = storage->GetFloat(id, *v ? 1.0f : 0.0f);

                const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, height));
                if (clicked) *v = !*v;

                // DeltaTime ベースの指数補間（フレームレート非依存）
                anim_t += ((*v ? 1.0f : 0.0f) - anim_t) * ImSaturate(ImGui::GetIO().DeltaTime * 10.0f);
                storage->SetFloat(id, anim_t);

                const bool hovered = ImGui::IsItemHovered();

                // トラック色: ON/OFF を anim_t で補間
                // GetColorU32(ImVec4) が style.Alpha（BeginDisabled 含む）を自動適用する
                const ImVec4 trackOff = hovered ? ImVec4(95 / 255.f, 95 / 255.f, 100 / 255.f, 1.f) : ImVec4(70 / 255.f, 70 / 255.f, 75 / 255.f, 1.f);
                const ImVec4 trackOn = hovered ? ImVec4(52 / 255.f, 215 / 255.f, 105 / 255.f, 1.f) : ImVec4(34 / 255.f, 197 / 255.f, 94 / 255.f, 1.f);
                const ImU32 col_track = ImGui::GetColorU32(ImLerp(trackOff, trackOn, anim_t));
                const ImU32 col_border = ImGui::GetColorU32(ImLerp(
                    ImVec4(45 / 255.f, 45 / 255.f, 50 / 255.f, 140 / 255.f),
                    ImVec4(20 / 255.f, 155 / 255.f, 65 / 255.f, 140 / 255.f),
                    anim_t));

                // ノブ中心座標（anim_t でスムーズに移動）
                const float knobMinX = p.x + knobPad + knobR;
                const float knobMaxX = p.x + width - knobPad - knobR;
                const float knobCX = knobMinX + anim_t * (knobMaxX - knobMinX);
                const float knobCY = p.y + radius;

                // トラック（塗り + 枠線）
                draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_track, radius);
                draw_list->AddRect(p, ImVec2(p.x + width, p.y + height), col_border, radius, 0, 1.2f);

                // ノブ落ち影 + ノブ本体
                draw_list->AddCircleFilled(ImVec2(knobCX, knobCY + 1.0f), knobR, ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 50 / 255.f)));
                draw_list->AddCircleFilled(ImVec2(knobCX, knobCY), knobR, ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));

                // "##" 始まりは ImGui の慣習に従いラベルを非表示
                if (label[0] != '#') {
                    ImGui::SameLine();
                    ImGui::Text("%s", label);
                }

                return clicked;
            }

        } // namespace Widgets
    } // namespace UI
} // namespace CoreEngine

#endif // USE_IMGUI
