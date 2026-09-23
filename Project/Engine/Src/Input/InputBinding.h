#pragma once
#include "IInputDevice.h"
#include "MouseInput.h"
#include "GamepadInput.h"
#include <cstdint>
#include <string>

namespace CoreEngine {

    /// @brief バインディングのデバイス種別
    enum class BindingType : uint8_t {
        Keyboard = 0, ///< キーボード（DIK_* コード）
        MouseButton,  ///< マウスボタン
        GamepadButton,///< ゲームパッドボタン
        GamepadAxis,  ///< ゲームパッドアナログ軸
    };

    /// @brief 一緒に押しておく必要のあるキー
    /// @details `Ctrl+Key:S` のような組み合わせを表す。
    /// @note 指定したものが押されていることだけを見る。指定していないキーは問わないので、
    ///       `Key:W` は Shift を押しながらでも効く（ダッシュしながら歩けるようにするため）。
    enum class InputModifier : uint8_t {
        None  = 0,
        Ctrl  = 1 << 0,
        Shift = 1 << 1,
        Alt   = 1 << 2,
    };

    constexpr InputModifier operator|(InputModifier a, InputModifier b)
    {
        return static_cast<InputModifier>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    constexpr InputModifier& operator|=(InputModifier& a, InputModifier b) { a = a | b; return a; }

    /// @brief 指定した修飾キーをすべて含むか
    constexpr bool HasModifier(InputModifier value, InputModifier required)
    {
        return (static_cast<uint8_t>(value) & static_cast<uint8_t>(required))
            == static_cast<uint8_t>(required);
    }

    /// @brief 物理入力1件のバインディング定義
    struct InputBinding {
        BindingType type = BindingType::Keyboard;
        uint16_t    code = 0;      ///< DIK_* / MouseButton / GamepadButton / GamepadAxis の値
        float       axisSign = 1.0f;   ///< アナログ軸の向き（+1.0 または -1.0）
        InputModifier modifiers = InputModifier::None; ///< 一緒に押しておくキー

        // ─── ファクトリ関数 ───────────────────────────────────────

        /// @brief キーボードバインディングを作成
        /// @param dikCode DIK_* 定数
        static InputBinding FromKey(uint8_t dikCode);

        /// @brief マウスボタンバインディングを作成
        static InputBinding FromMouseButton(MouseButton button);

        /// @brief ゲームパッドボタンバインディングを作成
        static InputBinding FromGamepadButton(GamepadButton button);

        /// @brief ゲームパッドアナログ軸バインディングを作成
        /// @param axis 軸の種類
        /// @param positive true で正方向、false で負方向
        static InputBinding FromGamepadAxis(GamepadAxis axis, bool positive = true);

        // ─── シリアライズ ─────────────────────────────────────────

        /// @brief JSON 保存用の文字列に変換（`Ctrl+Key:S` のように修飾キーを前に付ける）
        std::string Serialize() const;

        /// @brief 修飾キーを除いた本体だけの綴り
        std::string SerializeBody() const;

        /// @brief 文字列からバインディングを復元（前に付いた修飾キーも読む）
        static InputBinding Deserialize(const std::string& str);

        /// @brief 修飾キーを除いた本体だけを読む
        static InputBinding DeserializeBody(const std::string& str);
    };

}
