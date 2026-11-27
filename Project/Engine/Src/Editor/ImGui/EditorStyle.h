#pragma once

#include "Editor/ImGui/EditorTheme.h"

#include <imgui.h>
#include <implot.h>
#include <filesystem>
#include <iterator>

/// @file EditorStyle.h
/// @brief ImGui と ImPlot の見た目と字をエディタのものにする（エディタと CoreEngine Profiler が使う）

namespace CoreEngine::Editor::Theme
{
    /// @brief ImGui の色・余白・角丸・線の太さをエディタの見た目にする
    inline void ApplyImGuiStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // ===== 配色 =====
        // 方針: ①背景に明度の段差を作る ②アクセント色は「選択・操作中」だけに使う
        //       ③面の区切りは枠線ではなく明度差で行う（FrameBorderSize = 0）
        //
        // 配色の実体は Theme が持つ（sRGB の 0-255 値をリニアへ逆変換したもの）。
        // ここでは ImGui の色スロットへ割り当てるだけにする。

        // 背景の階調（暗 → 明）
        const ImVec4 bgDeepest = kDeepest;    // 最奥（スクロールバー溝など）
        const ImVec4 bgField = kField;      // 入力欄（一段沈める）
        const ImVec4 bgChild = kChild;      // 子パネル（少し沈める）
        const ImVec4 bgWindow = kWindow;     // ウィンドウ
        const ImVec4 bgPanel = kPanel;      // メニューバー・タイトル・ポップアップ
        const ImVec4 bgControl = kControl;    // ボタン・タブ選択
        const ImVec4 bgHover = kHover;      // ホバー
        const ImVec4 bgActive = kActive;     // 押下

        // アクセント（青。選択・操作中の要素だけに使う）
        const ImVec4 accent = kAccent;
        const ImVec4 accentHover = kAccentHover;
        const ImVec4 accentMuted = kAccentMuted; // 選択行の下地
        const ImVec4 accentWarm = kWarm;         // 差し色（少量だけ使う）

        // テキスト
        const ImVec4 textPrimary = kText;
        const ImVec4 textDisabled = kTextMute;

        const ImVec4 borderColor = kBorder;
        const ImVec4 transparent = kTransparent;

        // ===== 面 =====
        colors[ImGuiCol_WindowBg] = bgWindow;
        colors[ImGuiCol_ChildBg] = bgChild;
        colors[ImGuiCol_PopupBg] = bgPanel;
        colors[ImGuiCol_MenuBarBg] = bgPanel;
        colors[ImGuiCol_Border] = borderColor;
        colors[ImGuiCol_BorderShadow] = transparent;

        // ===== 文字 =====
        colors[ImGuiCol_Text] = textPrimary;
        colors[ImGuiCol_TextDisabled] = textDisabled;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        colors[ImGuiCol_TextLink] = accentHover;
        colors[ImGuiCol_InputTextCursor] = textPrimary;

        // ===== タイトルバー =====
        colors[ImGuiCol_TitleBg] = bgPanel;
        colors[ImGuiCol_TitleBgActive] = bgPanel;
        colors[ImGuiCol_TitleBgCollapsed] = bgWindow;

        // ===== タブ =====
        // 選択タブは「明るい面 ＋ 上端の細いアクセント線」で示す。
        colors[ImGuiCol_Tab] = bgPanel;
        colors[ImGuiCol_TabHovered] = bgHover;
        colors[ImGuiCol_TabSelected] = bgControl;
        colors[ImGuiCol_TabSelectedOverline] = accentHover;
        colors[ImGuiCol_TabDimmed] = bgChild;
        colors[ImGuiCol_TabDimmedSelected] = bgPanel;
        colors[ImGuiCol_TabDimmedSelectedOverline] = accentMuted;

        // ===== ヘッダ（Selectable / TreeNode / CollapsingHeader 共用） =====
        // 選択行としても折りたたみ見出しとしても使える、彩度を落とした青灰色にする。
        colors[ImGuiCol_Header] = accentMuted;
        colors[ImGuiCol_HeaderHovered] = bgHover;
        colors[ImGuiCol_HeaderActive] = accent;

        // ===== 入力欄 =====
        colors[ImGuiCol_FrameBg] = bgField;
        colors[ImGuiCol_FrameBgHovered] = kFieldHover;
        colors[ImGuiCol_FrameBgActive] = kFieldActive;

        // ===== ボタン =====
        colors[ImGuiCol_Button] = bgControl;
        colors[ImGuiCol_ButtonHovered] = bgHover;
        colors[ImGuiCol_ButtonActive] = bgActive;

        // ===== 操作子 =====
        colors[ImGuiCol_CheckMark] = accentHover;
        colors[ImGuiCol_SliderGrab] = accent;
        colors[ImGuiCol_SliderGrabActive] = accentHover;

        colors[ImGuiCol_ScrollbarBg] = bgDeepest;
        colors[ImGuiCol_ScrollbarGrab] = FromSrgb(63, 63, 70);
        colors[ImGuiCol_ScrollbarGrabHovered] = bgHover;
        colors[ImGuiCol_ScrollbarGrabActive] = bgActive;

        colors[ImGuiCol_Separator] = FromSrgb(56, 56, 62);
        colors[ImGuiCol_SeparatorHovered] = accent;
        colors[ImGuiCol_SeparatorActive] = accentHover;

        // リサイズグリップは掴むまで見せない
        colors[ImGuiCol_ResizeGrip] = transparent;
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_ResizeGripActive] = accent;

        // ===== グラフ =====
        colors[ImGuiCol_PlotLines] = accentHover;
        colors[ImGuiCol_PlotLinesHovered] = accentWarm;
        colors[ImGuiCol_PlotHistogram] = accentWarm;
        colors[ImGuiCol_PlotHistogramHovered] = FromSrgb(255, 204, 120);

        // ===== テーブル =====
        colors[ImGuiCol_TableHeaderBg] = bgPanel;
        colors[ImGuiCol_TableBorderStrong] = FromSrgb(60, 60, 66);
        colors[ImGuiCol_TableBorderLight] = FromSrgb(42, 42, 47);
        colors[ImGuiCol_TableRowBg] = transparent;
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.022f);

        // ===== その他 =====
        colors[ImGuiCol_DragDropTarget] = accentWarm;
        colors[ImGuiCol_NavCursor] = accentHover;
        colors[ImGuiCol_NavWindowingHighlight] = accentHover;
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
        colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = bgDeepest;

        // ===== 形状・余白 =====
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        style.WindowRounding = 4.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;  // 全ウィジェットの黒枠をやめる
        style.TabBarBorderSize = 1.0f;
        style.TabBarOverlineSize = 2.0f;
        style.DockingSeparatorSize = 2.0f;

        // 見出し（SeparatorText）を左寄せの太線にして、セクションの区切りを読みやすくする
        style.SeparatorTextBorderSize = 2.0f;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(16.0f, 6.0f);

        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None; // タイトル左の折りたたみ矢印を消す

        style.Alpha = 1.0f;
    }

    /// @brief ImPlot のグラフの面・軸・凡例の色をエディタのテーマに合わせ、系列の 8 色を既定の色の並びにする
    /// @details ImPlot のコンテキストを作った後に呼ぶ
    inline void ApplyPlotStyle()
    {
        ImPlotStyle& style = ImPlot::GetStyle();
        style.Colors[ImPlotCol_FrameBg] = kTransparent;
        style.Colors[ImPlotCol_PlotBg] = kField;
        style.Colors[ImPlotCol_PlotBorder] = kTransparent;
        style.Colors[ImPlotCol_LegendBg] = WithAlpha(kPanel, 0.92f);
        style.Colors[ImPlotCol_LegendBorder] = kOutline;
        style.Colors[ImPlotCol_LegendText] = kText;
        style.Colors[ImPlotCol_InlayText] = kText;
        style.Colors[ImPlotCol_AxisText] = kTextDim;
        style.Colors[ImPlotCol_AxisGrid] = kControl;
        style.Colors[ImPlotCol_AxisTick] = kControl;

        style.Colormap = ImPlot::AddColormap("CoreEngine", kSeries, static_cast<int>(std::size(kSeries)));
    }

    /// @brief 游ゴシック Bold（12px × 拡大率）を既定の字にし、游ゴシックに無い記号を Segoe UI Symbol から重ねる
    /// @param dpiScale 窓のあるモニタの拡大率（1.0 = 100%）
    /// @return 游ゴシックのファイルが無く、読み込めなかったら false
    /// @note 字の画像は作らない。動的な字の画像に対応していない裏方では、呼んだ側で io.Fonts->Build() を呼ぶ
    inline bool LoadFonts(float dpiScale)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float baseFontSize = 12.0f;

        ImFontConfig config = {};
        config.SizePixels = baseFontSize * dpiScale;

        // 日本語グリフ範囲に加え、罫線文字・記号を追加
        static const ImWchar kExtraRanges[] = {
            0x2022, 0x2022, // Bullet •
            0x2500, 0x257F, // Box Drawing
            0x2580, 0x259F, // Block Elements
            0x25A0, 0x25FF, // Geometric Shapes
            0x2713, 0x2713, // Check mark
            0,
        };

        // UI の記号（矢印・再生記号・幾何学図形）。Yu Gothic は大半を持たないため、
        // これらだけを記号フォントから重ねて読む
        static const ImWchar kSymbolRanges[] = {
            0x2190, 0x21FF, // Arrows
            0x2200, 0x22FF, // Mathematical Operators
            0x23E9, 0x23FA, // Media Control Symbols
            0x25A0, 0x25FF, // Geometric Shapes
            0x2600, 0x26FF, // Miscellaneous Symbols
            0x2700, 0x27BF, // Dingbats
            0x27F0, 0x27FF, // Supplemental Arrows-A
            0x2900, 0x297F, // Supplemental Arrows-B
            0,
        };

        ImFontGlyphRangesBuilder rangesBuilder;
        rangesBuilder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        rangesBuilder.AddRanges(kExtraRanges);
        static ImVector<ImWchar> fontRanges;
        fontRanges.clear();
        rangesBuilder.BuildRanges(&fontRanges);

        const char* fontPath = "C:/Windows/Fonts/YuGothB.ttc";
        const char* symbolFontPath = "C:/Windows/Fonts/seguisym.ttf";

        if (!std::filesystem::exists(fontPath)) {
            return false;
        }

        ImFont* font = io.Fonts->AddFontFromFileTTF(
            fontPath,
            config.SizePixels,
            &config,
            fontRanges.Data);

        // 本文フォントに無い記号だけを同じフォントへ足す
        if (font && std::filesystem::exists(symbolFontPath)) {
            ImFontConfig symbolConfig = config;
            symbolConfig.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(
                symbolFontPath,
                config.SizePixels,
                &symbolConfig,
                kSymbolRanges);
        }

        if (!font) {
            return false;
        }
        io.FontDefault = font;
        io.FontGlobalScale = 1.0f;
        return true;
    }
}
