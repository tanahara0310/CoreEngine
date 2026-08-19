#pragma once
#include "IInputDevice.h"
#include <Xinput.h>

namespace CoreEngine
{
    /// @brief ゲームパッドのボタン（値は XInput の定義に一致させてある）
    enum class GamepadButton {
        A = XINPUT_GAMEPAD_A,
        B = XINPUT_GAMEPAD_B,
        X = XINPUT_GAMEPAD_X,
        Y = XINPUT_GAMEPAD_Y,
        DPadUp = XINPUT_GAMEPAD_DPAD_UP,
        DPadDown = XINPUT_GAMEPAD_DPAD_DOWN,
        DPadLeft = XINPUT_GAMEPAD_DPAD_LEFT,
        DPadRight = XINPUT_GAMEPAD_DPAD_RIGHT,
        Start = XINPUT_GAMEPAD_START,
        Back = XINPUT_GAMEPAD_BACK,
        LeftThumb = XINPUT_GAMEPAD_LEFT_THUMB,
        RightThumb = XINPUT_GAMEPAD_RIGHT_THUMB,
        LeftShoulder = XINPUT_GAMEPAD_LEFT_SHOULDER,
        RightShoulder = XINPUT_GAMEPAD_RIGHT_SHOULDER,
    };

    /// @brief スティックの状態を表す構造体
    struct Stick {
        float x;
        float y;
    };

    /// @brief ゲームパッド入力クラス（XInputベース、IInputDevice直継承）
    class GamepadInput : public IInputDevice {
    public:
        /// @brief 初期化
        /// @param playerIndex XInputプレイヤーインデックス（0〜3）
        void Initialize(DWORD playerIndex = 0);

        /// @brief ゲームパッドの状態を更新
        void Update() override;

        /// @brief コントローラが接続されているか
        bool IsConnected() const;

        /// @brief ボタンが押され続けているか
        bool  IsButtonPressed(GamepadButton button) const;
        /// @brief ボタンが押された瞬間か
        bool  IsButtonTriggered(GamepadButton button) const;
        /// @brief ボタンが離された瞬間か
        bool  IsButtonReleased(GamepadButton button) const;

        /// @brief 左スティックの状態を取得
        Stick GetLeftStick(float deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) const;

        /// @brief 右スティックの状態を取得
        Stick GetRightStick(float deadZone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) const;

        /// @brief 振動を設定（0.0〜1.0）
        void SetVibration(float leftMotorRatio, float rightMotorRatio);

        /// @brief 左トリガーの踏み込み量（0.0〜1.0）
        float GetLeftTrigger() const;
        /// @brief 右トリガーの踏み込み量（0.0〜1.0）
        float GetRightTrigger() const;

    private:
        DWORD         padIndex_ = 0;
        XINPUT_STATE  state_ = {};
        XINPUT_STATE  prevState_ = {};
        bool          isConnected_ = false;
    };
}
