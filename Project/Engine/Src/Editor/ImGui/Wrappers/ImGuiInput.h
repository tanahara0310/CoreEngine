#pragma once

#ifdef USE_IMGUI

#include <imgui.h>
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine {
    namespace UI {

        // ─────────────── ドラッグ入力（min/max は 0 で制限なし）───────────────

        /// @brief Vector3 を直接渡せる DragFloat3 ラッパー
        inline bool DragVec3(const char* label, Vector3& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat3(label, &v.x, speed, min, max);
        }

        /// @brief Vector2 を直接渡せる DragFloat2 ラッパー
        inline bool DragVec2(const char* label, Vector2& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat2(label, &v.x, speed, min, max);
        }

        /// @brief float の DragFloat ラッパー
        inline bool DragFloat(const char* label, float& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f,
            const char* format = "%.3f")
        {
            return ImGui::DragFloat(label, &v, speed, min, max, format);
        }

        /// @brief int の DragInt ラッパー
        inline bool DragInt(const char* label, int& v,
            float speed = 1.0f, int min = 0, int max = 0)
        {
            return ImGui::DragInt(label, &v, speed, min, max);
        }

        // ─────────────── スライダー入力 ───────────────

        /// @brief float のスライダー
        inline bool SliderFloat(const char* label, float& v, float min, float max,
            const char* format = "%.3f")
        {
            return ImGui::SliderFloat(label, &v, min, max, format);
        }

        /// @brief int のスライダー
        inline bool SliderInt(const char* label, int& v, int min, int max)
        {
            return ImGui::SliderInt(label, &v, min, max);
        }

        // ─────────────── テキスト入力 ───────────────

        /// @brief 文字列入力ボックス（InputText ラッパー）
        inline bool InputText(const char* label, char* buf, size_t buf_size,
            ImGuiInputTextFlags flags = 0)
        {
            return ImGui::InputText(label, buf, buf_size, flags);
        }

        /// @brief プレースホルダー付き文字列入力ボックス
        inline bool InputTextWithHint(const char* label, const char* hint,
            char* buf, size_t buf_size,
            ImGuiInputTextFlags flags = 0)
        {
            return ImGui::InputTextWithHint(label, hint, buf, buf_size, flags);
        }

        // ─────────────── カラー入力 ───────────────

        /// @brief Vector4 を直接渡せる ColorEdit4 ラッパー（RGBA）
        inline bool ColorEdit(const char* label, Vector4& color,
            ImGuiColorEditFlags flags = 0)
        {
            return ImGui::ColorEdit4(label, &color.x, flags);
        }

        /// @brief Vector3 を直接渡せる ColorEdit3 ラッパー（RGB、アルファなし）
        /// @note カメラ軌跡色など Vector3 で色を管理する場合に使用する
        inline bool ColorEdit3(const char* label, Vector3& color,
            ImGuiColorEditFlags flags = 0)
        {
            return ImGui::ColorEdit3(label, &color.x, flags);
        }

        // ─────────────── 4要素ドラッグ ───────────────

        /// @brief Vector4 を直接渡せる DragFloat4 ラッパー（クォータニオンなど）
        inline bool DragVec4(const char* label, Vector4& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat4(label, &v.x, speed, min, max);
        }

    } // namespace UI
} // namespace CoreEngine

#endif // USE_IMGUI
