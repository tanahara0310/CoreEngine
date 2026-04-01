#pragma once

#ifdef USE_IMGUI

#include <imgui.h>
#include <utility>

namespace CoreEngine {
    namespace UI {

        // ─────────────── テキスト表示 ───────────────

        /// @brief 通常テキストを表示する（Text ラッパー）
        inline void Label(const char* text)
        {
            ImGui::Text("%s", text);
        }

        /// @brief グレーの補足テキストを表示する（TextDisabled ラッパー）
        inline void Hint(const char* text)
        {
            ImGui::TextDisabled("%s", text);
        }

        // ─────────────── セクション区切り ───────────────

        /// @brief セクション見出し付き水平線を描画する（SeparatorText ラッパー）
        inline void SectionHeader(const char* label)
        {
            ImGui::SeparatorText(label);
        }

        /// @brief 水平線を描画する（Separator ラッパー）
        inline void Separator()
        {
            ImGui::Separator();
        }

        // ─────────────── レイアウト ───────────────

        /// @brief 次のウィジェットを同じ行に配置する（SameLine ラッパー）
        /// @param offsetFromStartX  行頭からのオフセット（0.0f = 直前の項目の直後）
        inline void SameLine(float offsetFromStartX = 0.0f)
        {
            ImGui::SameLine(offsetFromStartX);
        }

        /// @brief 垂直方向の余白を追加する（Spacing ラッパー）
        inline void Spacing()
        {
            ImGui::Spacing();
        }

        // ─────────────── ツールチップ ───────────────

        /// @brief 直前のウィジェットにホバー時ツールチップを設定する
        inline void Tooltip(const char* text)
        {
            ImGui::SetItemTooltip("%s", text);
        }

                /// @brief フォーマット付きグレーの補足テキストを表示する
                /// @note UI::HintF("%d/%d", current, total) のように printf 形式で使う
                template<typename... Args>
                inline void HintF(const char* fmt, Args&&... args)
                {
                    ImGui::TextDisabled(fmt, std::forward<Args>(args)...);
                }

                // ─────────────── プログレスバー ───────────────

                /// @brief プログレスバーを描画する
                /// @param fraction  進捗割合（0.0f〜1.0f）
                /// @param size      バーのサイズ（x=-1 で幅いっぱい、y=-1 でデフォルト高さ）
                /// @param overlay   バー上に重ねて表示するテキスト（nullptr で表示なし）
                inline void ProgressBar(float fraction,
                    ImVec2 size = ImVec2(-FLT_MIN, 0),
                    const char* overlay = nullptr)
                {
                    ImGui::ProgressBar(fraction, size, overlay);
                }

            } // namespace UI
    } // namespace CoreEngine

    #endif // USE_IMGUI
