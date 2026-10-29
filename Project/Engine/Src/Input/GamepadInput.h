#pragma once
#include "IInputDevice.h"
#include <cstdint>

struct SDL_Gamepad;

namespace CoreEngine
{
    /// @brief ゲームパッドのボタン（Xbox の並び。PS や Switch のパッドも同じ位置のボタンを返す）
    enum class GamepadButton {
        A = 0x1000,
        B = 0x2000,
        X = 0x4000,
        Y = 0x8000,
        DPadUp = 0x0001,
        DPadDown = 0x0002,
        DPadLeft = 0x0004,
        DPadRight = 0x0008,
        Start = 0x0010,
        Back = 0x0020,
        LeftThumb = 0x0040,
        RightThumb = 0x0080,
        LeftShoulder = 0x0100,
        RightShoulder = 0x0200,
    };

    /// @brief スティックの状態を表す構造体
    struct Stick {
        float x;
        float y;
    };

    /// @brief ゲームパッドのアナログ軸
    enum class GamepadAxis : uint8_t {
        LeftStickX = 0,
        LeftStickY,
        RightStickX,
        RightStickY,
        LeftTrigger,
        RightTrigger,
    };

    /// @brief 左スティックのデッドゾーン（-32768〜32767 の目盛り）
    inline constexpr float kLeftStickDeadZone = 7849.0f;
    /// @brief 右スティックのデッドゾーン（-32768〜32767 の目盛り）
    inline constexpr float kRightStickDeadZone = 8689.0f;

    /// @brief ゲームパッド入力クラス（SDL3 のゲームパッドを 1 台受け持つ）
    /// @details つなぐパッドは InputManager が割り当てる（Attach / Detach）。
    class GamepadInput : public IInputDevice {
    public:
        /// @brief 初期化
        /// @param playerIndex プレイヤー番号（0〜3）
        void Initialize(DWORD playerIndex = 0);

        /// @brief パッドを受け持つ
        void Attach(SDL_Gamepad* gamepad);

        /// @brief 受け持ちを外す（振動を止める）
        /// @return 外したパッド（閉じるのは呼び出し側）
        SDL_Gamepad* Detach();

        /// @brief 受け持っているパッド（無ければ nullptr）
        SDL_Gamepad* GetGamepad() const { return gamepad_; }

        /// @brief ゲームパッドの状態を更新
        void Update() override;

        /// @brief 全ボタンを離し、スティックとトリガーを 0 にする
        void Reset() override;

        /// @brief コントローラが接続されているか
        bool IsConnected() const;

        /// @brief ボタンが押され続けているか
        bool  IsButtonPressed(GamepadButton button) const;
        /// @brief ボタンが押された瞬間か
        bool  IsButtonTriggered(GamepadButton button) const;
        /// @brief ボタンが離された瞬間か
        bool  IsButtonReleased(GamepadButton button) const;

        /// @brief 左スティックの状態を取得
        Stick GetLeftStick(float deadZone = kLeftStickDeadZone) const;

        /// @brief 右スティックの状態を取得
        Stick GetRightStick(float deadZone = kRightStickDeadZone) const;

        /// @brief アナログ軸の現在値を取得（スティックは -1.0〜1.0、トリガーは 0.0〜1.0）
        float GetAxisValue(GamepadAxis axis) const;

        /// @brief アナログ軸がしきい値を初めて超えたフレームか
        /// @details スティックを倒し続けても毎フレーム true にはならない。
        bool IsAxisTriggered(GamepadAxis axis, bool positive, float threshold = 0.1f) const;

        /// @brief アナログ軸がしきい値を下回ったフレームか（倒していたのを戻した）
        bool IsAxisReleased(GamepadAxis axis, bool positive, float threshold = 0.1f) const;

        /// @brief 振動を設定（0.0〜1.0）。止めるときは 0 を渡す
        void SetVibration(float leftMotorRatio, float rightMotorRatio);

        /// @brief 振動させてよいか。false の間はモーターを止め、true に戻すと設定済みの強さで再開する
        void SetVibrationAllowed(bool allowed);

        /// @brief 左トリガーの踏み込み量（0.0〜1.0）
        float GetLeftTrigger() const;
        /// @brief 右トリガーの踏み込み量（0.0〜1.0）
        float GetRightTrigger() const;

    private:
        /// @brief 1 フレーム分のパッドの状態（スティックは上と右が正）
        struct PadState {
            uint16_t buttons = 0;
            int16_t  leftX = 0;
            int16_t  leftY = 0;
            int16_t  rightX = 0;
            int16_t  rightY = 0;
            float    leftTrigger = 0.0f;
            float    rightTrigger = 0.0f;
        };

        float GetPreviousAxisValue(GamepadAxis axis) const;

        /// @brief 今の強さをパッドへ送る（同じ強さは効き目が切れる前の送り直しだけ）
        void SendVibration();

        DWORD         padIndex_ = 0;
        SDL_Gamepad*  gamepad_ = nullptr;
        PadState      state_ = {};
        PadState      prevState_ = {};
        bool          isConnected_ = false;

        bool          canRumble_ = false;
        float         vibrationLeft_ = 0.0f;
        float         vibrationRight_ = 0.0f;
        bool          vibrationAllowed_ = false;
        uint16_t      sentLeftMotor_ = 0;
        uint16_t      sentRightMotor_ = 0;
        uint64_t      sentAtMs_ = 0;
    };
}
