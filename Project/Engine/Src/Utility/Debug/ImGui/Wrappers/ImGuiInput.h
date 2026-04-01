#pragma once

#ifdef _DEBUG

#include <imgui.h>
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine {
    namespace UI {

        // ─────────────── ドラッグ入力 ───────────────

        /// @brief Vector3 を直接渡せる DragFloat3 ラッパー
        /// @param label  ラベル
        /// @param v      編集する Vector3
        /// @param speed  ドラッグ速度
        /// @param min    最小値（0.0f = 制限なし）
        /// @param max    最大値（0.0f = 制限なし）
        inline bool DragVec3(const char* label, Vector3& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat3(label, &v.x, speed, min, max);
        }

        /// @brief Vector2 を直接渡せる DragFloat2 ラッパー
        /// @param label  ラベル
        /// @param v      編集する Vector2
        /// @param speed  ドラッグ速度
        /// @param min    最小値（0.0f = 制限なし）
        /// @param max    最大値（0.0f = 制限なし）
        inline bool DragVec2(const char* label, Vector2& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat2(label, &v.x, speed, min, max);
        }

        /// @brief float の DragFloat ラッパー
        /// @param label  ラベル
        /// @param v      編集する float
        /// @param speed  ドラッグ速度
        /// @param min    最小値（0.0f = 制限なし）
        /// @param max    最大値（0.0f = 制限なし）
        inline bool DragFloat(const char* label, float& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f,
            const char* format = "%.3f")
        {
            return ImGui::DragFloat(label, &v, speed, min, max, format);
        }

        /// @brief int の DragInt ラッパー
        /// @param label  ラベル
        /// @param v      編集する int
        /// @param speed  ドラッグ速度
        /// @param min    最小値（0 = 制限なし）
        /// @param max    最大値（0 = 制限なし）
        inline bool DragInt(const char* label, int& v,
            float speed = 1.0f, int min = 0, int max = 0)
        {
            return ImGui::DragInt(label, &v, speed, min, max);
        }

        // ─────────────── スライダー入力 ───────────────

        /// @brief float のスライダー
        /// @param label  ラベル
        /// @param v      編集する float
        /// @param min    最小値
        /// @param max    最大値
        inline bool SliderFloat(const char* label, float& v, float min, float max,
            const char* format = "%.3f")
        {
            return ImGui::SliderFloat(label, &v, min, max, format);
        }

        /// @brief int のスライダー
        /// @param label  ラベル
        /// @param v      編集する int
        /// @param min    最小値
        /// @param max    最大値
        inline bool SliderInt(const char* label, int& v, int min, int max)
        {
            return ImGui::SliderInt(label, &v, min, max);
        }

        // ─────────────── テキスト入力 ───────────────

        /// @brief 文字列入力ボックス（InputText ラッパー）
        /// @param label    ラベル
        /// @param buf      入力バッファ（char 配列）
        /// @param buf_size バッファサイズ
        /// @param flags    ImGuiInputTextFlags（省略可）
        inline bool InputText(const char* label, char* buf, size_t buf_size,
            ImGuiInputTextFlags flags = 0)
        {
            return ImGui::InputText(label, buf, buf_size, flags);
        }

        /// @brief プレースホルダー付き文字列入力ボックス
        /// @param label    ラベル
        /// @param hint     未入力時に薄く表示するプレースホルダー
        /// @param buf      入力バッファ
        /// @param buf_size バッファサイズ
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
        /// @param label  ラベル
        /// @param v      編集する Vector4
        /// @param speed  ドラッグ速度
        /// @param min    最小値（0.0f = 制限なし）
        /// @param max    最大値（0.0f = 制限なし）
        inline bool DragVec4(const char* label, Vector4& v,
            float speed = 1.0f, float min = 0.0f, float max = 0.0f)
        {
            return ImGui::DragFloat4(label, &v.x, speed, min, max);
        }

    } // namespace UI
} // namespace CoreEngine

#endif // _DEBUG
