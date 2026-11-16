#pragma once

#ifdef CORE_EDITOR

#include <imgui.h>

#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

/// @file
/// @brief ドラッグ＆ドロップの中身を型で受け取る

namespace CoreEngine::UI
{
    /// @brief 今のドロップ先で種類 type のドロップを受け、中身を T として読む
    /// @tparam T std::string（ファイル名など）か、そのまま写せる型
    /// @return 落とされていなければ空。T が std::string 以外で中身の大きさが T と違うときも空
    /// @note std::string は中身を受け取った大きさの範囲の文字列にする（中身が無ければ空の文字列）
    template <class T>
    std::optional<T> AcceptPayload(const char* type)
    {
        const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(type);
        if (!payload) {
            return std::nullopt;
        }
        if constexpr (std::is_same_v<T, std::string>) {
            const auto* const data = static_cast<const char*>(payload->Data);
            if (!data || payload->DataSize <= 0) {
                return std::string();
            }
            return std::string(data, strnlen(data, static_cast<std::size_t>(payload->DataSize)));
        } else {
            static_assert(std::is_trivially_copyable_v<T>);
            if (payload->DataSize != static_cast<int>(sizeof(T))) {
                return std::nullopt;
            }
            T value{};
            std::memcpy(&value, payload->Data, sizeof(T));
            return value;
        }
    }
}

#endif // CORE_EDITOR
