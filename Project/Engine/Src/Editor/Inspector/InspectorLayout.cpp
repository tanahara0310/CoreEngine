#include "pch.h"
#include "Editor/Inspector/InspectorLayout.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/Widgets/EditorBars.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>

namespace CoreEngine::InspectorLayout
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// ラベルの列と欄の間の空き
        constexpr float kLabelGap = 8.0f;

        /// 小さな文字（ID）の大きさの倍率
        constexpr float kSmallTextScale = 0.85f;

        /// 見出しの丸の半径
        constexpr float kDotRadius = 3.5f;

        /// ⋮ の記号
        constexpr const char* kMenuGlyph = "⋮";

        /// @brief ラベルの列の幅（欄の幅に合わせて伸び縮みする）
        float LabelColumnWidth()
        {
            return std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 80.0f, 180.0f);
        }

        /// @brief 小さな文字の大きさを測る
        ImVec2 SmallTextSize(const char* text)
        {
            return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * kSmallTextScale, FLT_MAX, 0.0f, text);
        }

        /// @brief 小さな文字を描く
        void DrawSmallText(ImDrawList* drawList, const ImVec2& pos, const ImVec4& color, const char* text)
        {
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * kSmallTextScale, pos,
                ImGui::GetColorU32(color), text);
        }
    }

    bool DrawSectionHeader(const SectionHeader& header, bool& enabledChanged)
    {
        ImGuiStorage* const storage = ImGui::GetStateStorage();
        const ImGuiID openId = ImGui::GetID("##sectionOpen");
        bool open = storage->GetBool(openId, true);

        const ImGuiStyle& style = ImGui::GetStyle();
        const float fontSize = ImGui::GetFontSize();
        const float height = ImGui::GetFrameHeight() + 2.0f;
        const float extend = IM_TRUNC(style.WindowPadding.x * 0.5f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);

        // 見出し全体を押すと開閉する。後から置くチェックと ⋮ が先に押されるよう、重なりを許す
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##sectionHeader", ImVec2(width, height))) {
            open = !open;
            storage->SetBool(openId, open);
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (header.menuId) {
            ImGui::OpenPopupOnItemClick(header.menuId, ImGuiPopupFlags_MouseButtonRight);
        }
        const ImVec2 next = ImGui::GetCursorScreenPos();

        // 下地は窓の左右の余白まで伸ばす
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec4& fill = held ? Theme::kHover : (hovered ? Theme::kControl : Theme::kPanel);
        drawList->AddRectFilled(ImVec2(origin.x - extend, origin.y),
            ImVec2(origin.x + width + extend, origin.y + height), ImGui::GetColorU32(fill));

        const float centerY = origin.y + height * 0.5f;
        const float textY = centerY - fontSize * 0.5f;
        const float bottom = origin.y + height;
        float left = origin.x;

        // 開閉の矢印
        constexpr float kArrowScale = 0.7f;
        ImGui::RenderArrow(drawList, ImVec2(left, centerY - fontSize * 0.5f * kArrowScale),
            ImGui::GetColorU32(Theme::kTextDim), open ? ImGuiDir_Down : ImGuiDir_Right, kArrowScale);
        left += fontSize + 2.0f;

        // 出自の丸
        drawList->AddCircleFilled(ImVec2(left + kDotRadius, centerY), kDotRadius,
            ImGui::GetColorU32(header.origin == Origin::Script ? Theme::kScript : Theme::kAccentHover));
        left += kDotRadius * 2.0f + 7.0f;

        // 有効のチェック
        if (header.enabled) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
            const float box = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(left, centerY - box * 0.5f));
            if (ImGui::Checkbox("##enabled", header.enabled)) {
                enabledChanged = true;
            }
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", *header.enabled ? "有効（外すと止まる）" : "無効（入れると動く）");
            }
            left += box + 7.0f;
        }

        // 右端から ⋮ と札を置く
        float right = origin.x + width;
        if (header.menuId) {
            const ImVec2 glyphSize = ImGui::CalcTextSize(kMenuGlyph);
            const float buttonWidth = glyphSize.x + 12.0f;
            right -= buttonWidth;
            ImGui::SetCursorScreenPos(ImVec2(right, origin.y));
            if (ImGui::InvisibleButton("##sectionMenu", ImVec2(buttonWidth, height))) {
                ImGui::OpenPopup(header.menuId);
            }
            const bool menuHovered = ImGui::IsItemHovered();
            if (menuHovered) {
                drawList->AddRectFilled(ImVec2(right + 1.0f, origin.y + 2.0f),
                    ImVec2(right + buttonWidth - 1.0f, bottom - 2.0f), ImGui::GetColorU32(Theme::kHover), 3.0f);
                ImGui::SetTooltip("メニュー（右クリックでも開く）");
            }
            drawList->AddText(ImVec2(right + (buttonWidth - glyphSize.x) * 0.5f, textY),
                ImGui::GetColorU32(menuHovered ? Theme::kText : Theme::kTextDim), kMenuGlyph);
            right -= 4.0f;
        }
        if (header.tag && header.tag[0] != '\0') {
            const ImVec2 tagSize = UI::Bar::TagSize(header.tag);
            // 名前の場所が無くなるほど長い札は出さない
            if (tagSize.x <= (right - left) * 0.6f) {
                right -= tagSize.x;
                UI::Bar::DrawTag(drawList, ImVec2(right, centerY - tagSize.y * 0.5f), header.tag,
                    header.origin == Origin::Script ? Theme::kScript : Theme::kTextMute);
                right -= 6.0f;
            }
        }

        // 名前（入りきらなければ省略記号で詰める）
        UI::Bar::EllipsizedText(drawList, ImVec2(left, textY), ImVec2(right, bottom), header.name, Theme::kText);

        ImGui::SetCursorScreenPos(next);
        return open;
    }

    bool BeginRow(const char* label, const ImVec4& color, const char* popupId)
    {
        const float labelWidth = LabelColumnWidth();
        const float startX = ImGui::GetCursorPosX();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        const float textWidth = (std::max)(1.0f, labelWidth - kLabelGap);

        UI::Bar::EllipsizedText(ImGui::GetWindowDrawList(),
            ImVec2(origin.x, origin.y + ImGui::GetStyle().FramePadding.y),
            ImVec2(origin.x + textWidth, origin.y + height), label, color);

        ImGui::Dummy(ImVec2(textWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        if (popupId) {
            ImGui::OpenPopupOnItemClick(popupId, ImGuiPopupFlags_MouseButtonRight);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
        return hovered;
    }

    void DrawReferencePreview(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
        const char* icon, const char* name, const char* idText, bool error)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float fontSize = ImGui::GetFontSize();
        const float textY = min.y + style.FramePadding.y;
        float left = min.x + style.FramePadding.x;
        float right = max.x - style.FramePadding.x * 0.5f;

        drawList->PushClipRect(min, max, true);
        if (idText && idText[0] != '\0') {
            const ImVec2 idSize = SmallTextSize(idText);
            // 名前の場所が無くなるほど狭いときは ID を出さない
            if (idSize.x <= (right - left) * 0.4f) {
                right -= idSize.x;
                DrawSmallText(drawList, ImVec2(right, textY + (fontSize - idSize.y) * 0.5f), Theme::kTextMute, idText);
                right -= 6.0f;
            }
        }
        if (icon) {
            drawList->AddText(ImVec2(left, textY), ImGui::GetColorU32(Theme::kAccentHover), icon);
            left += ImGui::CalcTextSize(icon).x + 5.0f;
        }
        UI::Bar::EllipsizedText(drawList, ImVec2(left, textY), ImVec2(right, max.y), name,
            error ? Theme::kError : Theme::kText);
        drawList->PopClipRect();
    }

    bool ReferenceField(const char* icon, const char* name, const char* idText)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size((std::max)(1.0f, ImGui::CalcItemWidth()), ImGui::GetFrameHeight());
        const ImVec2 max(min.x + size.x, min.y + size.y);

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
        DrawReferencePreview(drawList, min, max, icon, name, idText, false);

        ImGui::Dummy(size);
        return ImGui::IsItemHovered();
    }

    std::string ShortId(const std::string& id, std::size_t length)
    {
        return id.size() > length ? id.substr(0, length) + "…" : id;
    }

    const char* AssetGlyph(AssetType type)
    {
        switch (type) {
        case AssetType::Model:    return "▣";
        case AssetType::Texture:  return "▤";
        case AssetType::Prefab:   return "◈";
        case AssetType::Scene:    return "▦";
        case AssetType::Audio:    return "♪";
        case AssetType::Material: return "◍";
        case AssetType::Shader:   return "▧";
        case AssetType::Json:
        case AssetType::Csv:      return "▤";
        default:                  return "▫";
        }
    }

    void AlignToRight(float width)
    {
        const float available = ImGui::GetContentRegionAvail().x;
        if (available > width) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
        }
    }
}

#endif // CORE_EDITOR
