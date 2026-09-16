#pragma once

#ifdef USE_IMGUI

#include "Editor/ImGui/EditorTheme.h"
#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

/// @file
/// @brief メニューバー・ツールバー・ステータスバーと各パネルで使う小さな部品

namespace CoreEngine::UI::Bar
{
    namespace detail
    {
        /// @brief ボタンの面と輪郭を描く
        inline void DrawButtonFrame(const ImVec2& min, const ImVec2& max, bool on, bool hovered, bool held)
        {
            ImVec4 fill = on ? Editor::Theme::kAccent : Editor::Theme::kControl;
            if (held) {
                fill = on ? Editor::Theme::kAccent : Editor::Theme::kActive;
            } else if (hovered) {
                fill = on ? Editor::Theme::kAccentHover : Editor::Theme::kHover;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(min, max, ImGui::GetColorU32(fill), 4.0f);
            draw->AddRect(min, max,
                ImGui::GetColorU32(on ? Editor::Theme::kAccentHover : Editor::Theme::kOutline), 4.0f);
        }
    }

    /// @brief バーの縦の区切り線
    inline void Separator(float spacing = 7.0f)
    {
        ImGui::SameLine(0.0f, spacing);
        const float height = ImGui::GetFrameHeight() * 0.55f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float top = pos.y + (ImGui::GetFrameHeight() - height) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, top), ImVec2(pos.x, top + height),
            ImGui::GetColorU32(Editor::Theme::kOutline));
        ImGui::Dummy(ImVec2(1.0f, height));
        ImGui::SameLine(0.0f, spacing);
    }

    /// @brief チップ（枠付きの小さなラベル）の幅
    inline float ChipWidth(const char* text)
    {
        return ImGui::CalcTextSize(text).x + 14.0f;
    }

    /// @brief 枠付きの小さなラベルを今の位置へ描く
    /// @param textColor 文字の色
    /// @param outline 輪郭の色
    inline void Chip(const char* text, const ImVec4& textColor, const ImVec4& outline)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 size(textSize.x + 14.0f, textSize.y + 4.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float offsetY = (ImGui::GetFrameHeight() - size.y) * 0.5f;
        const ImVec2 min(pos.x, pos.y + offsetY);
        const ImVec2 max(min.x + size.x, min.y + size.y);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, ImGui::GetColorU32(Editor::Theme::kField), 3.0f);
        draw->AddRect(min, max, ImGui::GetColorU32(outline), 3.0f);
        draw->AddText(ImVec2(min.x + 7.0f, min.y + 2.0f), ImGui::GetColorU32(textColor), text);

        ImGui::Dummy(size);
    }

    /// 札の文字の大きさの倍率
    inline constexpr float kTagTextScale = 0.85f;

    /// @brief 札（輪郭だけの小さな文字）の大きさ
    inline ImVec2 TagSize(const char* text)
    {
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
            ImGui::GetFontSize() * kTagTextScale, FLT_MAX, 0.0f, text);
        return ImVec2(textSize.x + 8.0f, textSize.y + 2.0f);
    }

    /// @brief 札を描く
    /// @param min 左上
    /// @param color 文字の色（輪郭はこれを薄くした色）
    inline void DrawTag(ImDrawList* drawList, const ImVec2& min, const char* text, const ImVec4& color)
    {
        const ImVec2 size = TagSize(text);
        drawList->AddRect(min, ImVec2(min.x + size.x, min.y + size.y),
            ImGui::GetColorU32(Editor::Theme::WithAlpha(color, 0.4f)), 3.0f);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * kTagTextScale,
            ImVec2(min.x + 4.0f, min.y + 1.0f), ImGui::GetColorU32(color), text);
    }

    /// @brief 範囲に収まらない文字を省略記号で詰めて描く
    /// @param min 文字の左上
    /// @param max 描いてよい範囲の右下
    inline void EllipsizedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
                               const char* text, const ImVec4& color)
    {
        if (max.x <= min.x) {
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextEllipsis(drawList, min, max, max.x, text, nullptr, nullptr);
        ImGui::PopStyleColor();
    }

    /// @brief ツールバーのボタンの幅
    inline float ButtonWidth(const char* label)
    {
        return ImGui::CalcTextSize(label, nullptr, true).x + 18.0f;
    }

    /// @brief 状態を持つツールバーのボタン
    /// @param on 選択中ならアクセントの面で描く
    /// @return 押されたら true
    inline bool Button(const char* label, bool on, const char* tooltip = nullptr, bool enabled = true)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
        const ImVec2 size(textSize.x + 18.0f, ImGui::GetFrameHeight());
        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::InvisibleButton(label, size);
        ImGui::EndDisabled();

        const ImVec2 max(min.x + size.x, min.y + size.y);
        detail::DrawButtonFrame(min, max, on, ImGui::IsItemHovered(), ImGui::IsItemActive());

        const ImVec4 textColor = !enabled ? Editor::Theme::kTextMute
            : (on ? Editor::Theme::kText : Editor::Theme::kTextDim);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f),
            ImGui::GetColorU32(textColor), label, ImGui::FindRenderedTextEnd(label));

        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    }

    /// @brief 再生系ボタンの図形
    enum class Transport
    {
        Play,   ///< 右向きの三角
        Pause,  ///< 縦棒 2 本
        Step,   ///< 三角＋縦棒
    };

    /// @brief 再生・一時停止・コマ送りのボタン（図形は自前で描く）
    /// @param on 選択中ならアクセントの面で描く
    /// @return 押されたら true
    inline bool TransportButton(const char* id, Transport kind, bool on,
        const char* tooltip = nullptr, bool enabled = true)
    {
        const float height = ImGui::GetFrameHeight();
        const ImVec2 size(height * 1.35f, height);
        const ImVec2 min = ImGui::GetCursorScreenPos();

        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::InvisibleButton(id, size);
        ImGui::EndDisabled();

        const ImVec2 max(min.x + size.x, min.y + size.y);
        detail::DrawButtonFrame(min, max, on, ImGui::IsItemHovered(), ImGui::IsItemActive());

        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float r = height * 0.22f;

        ImVec4 tint = !enabled ? Editor::Theme::kTextMute
            : (on ? Editor::Theme::kText : Editor::Theme::kTextDim);
        if (kind == Transport::Play && !on && enabled) {
            tint = Editor::Theme::kOk;
        }
        const ImU32 color = ImGui::GetColorU32(tint);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        switch (kind) {
        case Transport::Play:
            draw->AddTriangleFilled(
                ImVec2(center.x - r * 0.75f, center.y - r),
                ImVec2(center.x + r, center.y),
                ImVec2(center.x - r * 0.75f, center.y + r), color);
            break;
        case Transport::Pause:
            draw->AddRectFilled(ImVec2(center.x - r * 0.8f, center.y - r),
                ImVec2(center.x - r * 0.2f, center.y + r), color);
            draw->AddRectFilled(ImVec2(center.x + r * 0.2f, center.y - r),
                ImVec2(center.x + r * 0.8f, center.y + r), color);
            break;
        case Transport::Step:
            draw->AddTriangleFilled(
                ImVec2(center.x - r, center.y - r),
                ImVec2(center.x + r * 0.3f, center.y),
                ImVec2(center.x - r, center.y + r), color);
            draw->AddRectFilled(ImVec2(center.x + r * 0.5f, center.y - r),
                ImVec2(center.x + r, center.y + r), color);
            break;
        }

        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    }
}

#endif // USE_IMGUI
