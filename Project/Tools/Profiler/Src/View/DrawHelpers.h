#pragma once

#include "Editor/ImGui/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

/// @file DrawHelpers.h
/// @brief 画面の部品（フレームの帯・タイムライン・選んだ区間の欄・表）で共通の描き方

namespace CoreEngine::Profiler::Draw
{
    namespace Theme = CoreEngine::Editor::Theme;

    /// @brief フレームの帯の「待ち」の色
    inline ImVec4 WaitBarColor()
    {
        return Theme::FromSrgb(51, 64, 79);
    }

    /// @brief 色の明るさ（リニアの色の輝度）
    inline float Luminance(const ImVec4& color)
    {
        return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
    }

    /// @brief その色の面に載せる字の色（明るい面には暗い字、暗い面には明るい字）
    inline ImU32 TextOn(const ImVec4& background)
    {
        return ImGui::GetColorU32(Luminance(background) > 0.18f ? Theme::kDeepest : Theme::kText);
    }

    /// @brief 色を暗くする（factor は 0〜1）
    inline ImVec4 Darken(const ImVec4& color, float factor)
    {
        return ImVec4(color.x * factor, color.y * factor, color.z * factor, color.w);
    }

    /// @brief 横の点線
    inline void DashedHLine(ImDrawList* drawList, float x0, float x1, float y, ImU32 color, float dash, float gap)
    {
        for (float x = x0; x < x1; x += dash + gap) {
            drawList->AddLine(ImVec2(x, y), ImVec2(std::min(x + dash, x1), y), color);
        }
    }

    /// @brief 斜線で埋めた四角（待ちの区間）
    inline void HatchedRect(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 base, ImU32 stripe, float spacing)
    {
        drawList->AddRectFilled(min, max, base);
        drawList->PushClipRect(min, max, true);
        const float height = max.y - min.y;
        for (float x = min.x - height; x < max.x; x += spacing) {
            drawList->AddLine(ImVec2(x, max.y), ImVec2(x + height, min.y), stripe, spacing * 0.45f);
        }
        drawList->PopClipRect();
    }

    /// @brief 文字の高さの真ん中に色の四角を描き、その右に薄い文字を出す（凡例の 1 つ）
    /// @param textOffsetY 同じ行の文字の下げ幅（AlignTextToFramePadding の行では FramePadding.y）
    inline void LegendItem(const ImVec4& color, const char* label, float textOffsetY = 0.0f, bool hatched = false)
    {
        const float size = ImGui::GetTextLineHeight() * 0.6f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float top = pos.y + textOffsetY + (ImGui::GetTextLineHeight() - size) * 0.5f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (hatched) {
            HatchedRect(drawList, ImVec2(pos.x, top), ImVec2(pos.x + size, top + size),
                ImGui::GetColorU32(Theme::kControl), ImGui::GetColorU32(Theme::FromSrgb(85, 85, 94)), size * 0.5f);
        } else {
            drawList->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + size, top + size), ImGui::GetColorU32(color));
        }
        ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }

    /// @brief 押した状態を下地の色で見せるボタン。swatch があれば文字の前に色の四角を描く
    inline bool ToggleButton(const char* label, bool selected, const ImVec4* swatch = nullptr)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? Theme::kAccentMuted : Theme::kControl);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? Theme::kAccentMuted : Theme::kHover);
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? Theme::kText : Theme::kTextDim);

        std::string text = label;
        float swatchSize = 0.0f;
        if (swatch) {
            swatchSize = ImGui::GetTextLineHeight() * 0.55f;
            const float spaceWidth = ImGui::CalcTextSize(" ").x;
            const int spaces = static_cast<int>(std::ceil((swatchSize + ImGui::GetStyle().ItemInnerSpacing.x) / spaceWidth));
            text.insert(0, static_cast<size_t>(spaces), ' ');
        }
        const bool pressed = ImGui::Button(text.c_str());
        if (swatch) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const float left = min.x + ImGui::GetStyle().FramePadding.x;
            const float top = (min.y + max.y - swatchSize) * 0.5f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(left + swatchSize, top + swatchSize),
                ImGui::GetColorU32(*swatch), 1.5f);
        }
        ImGui::PopStyleColor(3);
        return pressed;
    }

    /// @brief 強調したボタン（青の地）
    inline bool AccentButton(const char* label)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::FromSrgb(255, 255, 255));
        const bool pressed = ImGui::Button(label);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    /// @brief セルの右端に寄せて文字を出す
    inline void TextRight(const char* text, const ImVec4& color)
    {
        const float width = ImGui::CalcTextSize(text).x;
        const float space = ImGui::GetContentRegionAvail().x - width;
        if (space > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + space);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    /// @brief value 以上で、1・2・5 に 10 の累乗を掛けた最小の値（グラフの縦の上限）
    inline double NiceCeil(double value)
    {
        if (value <= 0.0) {
            return 1.0;
        }
        const double base = std::pow(10.0, std::floor(std::log10(value)));
        const double scaled = value / base;
        const double nice = scaled <= 1.0 ? 1.0 : (scaled <= 2.0 ? 2.0 : (scaled <= 5.0 ? 5.0 : 10.0));
        return nice * base;
    }
}
