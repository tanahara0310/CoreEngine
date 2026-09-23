#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief 入力を受け付ける場面
    /// @details 同じボタンをゲームと UI の両方へ配らないための仕切り。
    ///          アクションは「どの場面のものか」を持ち、今いる場面と重ならなければ
    ///          押されていないものとして扱う。
    /// @note これが無いと、決定ボタンでメニューを選んだ瞬間にキャラも跳ぶ。
    enum class InputContext : std::uint8_t {
        None   = 0,
        Game   = 1 << 0,  ///< 遊んでいる間の操作
        UI     = 1 << 1,  ///< メニューを選んでいる間の操作
        Editor = 1 << 2,  ///< エディタの操作（Release では立たない）
        All    = 0b111,
    };

    constexpr InputContext operator|(InputContext a, InputContext b)
    {
        return static_cast<InputContext>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    constexpr InputContext operator&(InputContext a, InputContext b)
    {
        return static_cast<InputContext>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    constexpr InputContext operator~(InputContext a)
    {
        return static_cast<InputContext>(~static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(InputContext::All));
    }

    constexpr InputContext& operator|=(InputContext& a, InputContext b) { a = a | b; return a; }
    constexpr InputContext& operator&=(InputContext& a, InputContext b) { a = a & b; return a; }

    /// @brief 重なりがあるか
    constexpr bool Overlaps(InputContext a, InputContext b) { return (a & b) != InputContext::None; }

    /// @brief 場面の綴り（"Game" / "UI" / "Editor"。複数は "Game|UI"）
    std::string InputContextToString(InputContext contexts);

    /// @brief 綴りから場面へ
    /// @param text "Game" / "UI" / "Editor" を `|` で繋いだもの
    /// @return 読めた場面。1 つも読めなければ `Game`
    InputContext InputContextFromString(std::string_view text);

    /// @brief 場面の表示名（"ゲーム" / "UI" / "エディタ"）
    std::string_view InputContextToDisplayName(InputContext context);

    /// @brief エディタのビルドでだけ立つ場面
    /// @details Release ではエディタの操作そのものが無いので落とす。
    constexpr InputContext kEditorContext =
#ifdef CORE_EDITOR
        InputContext::Editor;
#else
        InputContext::None;
#endif
}
