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

    /// @brief ゲームパッドアナログ軸の種別
    enum class GamepadAxis : uint8_t {
        LeftStickX = 0,
        LeftStickY,
        RightStickX,
        RightStickY,
        LeftTrigger,
        RightTrigger,
    };

    /// @brief 物理入力1件のバインディング定義
    struct InputBinding {
        BindingType type = BindingType::Keyboard;
        uint16_t    code = 0;      ///< DIK_* / MouseButton / GamepadButton / GamepadAxis の値
        float       axisSign = 1.0f;   ///< アナログ軸の向き（+1.0 または -1.0）

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

        /// @brief JSON 保存用の文字列に変換
        std::string Serialize() const;

        /// @brief 文字列からバインディングを復元
        static InputBinding Deserialize(const std::string& str);
    };

}
