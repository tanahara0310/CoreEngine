#include "pch.h"
#include "InputQuery.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "GamepadInput.h"

namespace CoreEngine {

void InputQuery::Initialize(KeyboardInput* keyboard, MouseInput* mouse, GamepadInput* gamepad) {
    keyboard_ = keyboard;
    mouse_    = mouse;
    gamepad_  = gamepad;
    config_.ResetToDefault();
}

// ─── アクションベース問い合わせ ───────────────────────────────

bool InputQuery::IsActionPressed(InputAction action) const {
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluatePressed(b)) return true;
    }
    return false;
}

bool InputQuery::IsActionTriggered(InputAction action) const {
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateTriggered(b)) return true;
    }
    return false;
}

bool InputQuery::IsActionReleased(InputAction action) const {
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateReleased(b)) return true;
    }
    return false;
}

float InputQuery::GetAxisValue(InputAction action) const {
    float maxVal = 0.0f;
    for (const auto& b : config_.GetBindings(action)) {
        const float val = EvaluateAxis(b);
        if (val > maxVal) maxVal = val;
    }
    return maxVal;
}

// ─── キーボード直接アクセス ───────────────────────────────────

bool InputQuery::IsKeyPressed(uint8_t dikCode) const {
    return keyboard_ && keyboard_->IsKeyPressed(dikCode);
}

bool InputQuery::IsKeyTriggered(uint8_t dikCode) const {
    return keyboard_ && keyboard_->IsKeyTriggered(dikCode);
}

bool InputQuery::IsKeyReleased(uint8_t dikCode) const {
    return keyboard_ && keyboard_->IsKeyReleased(dikCode);
}

// ─── マウス直接アクセス ───────────────────────────────────────

bool InputQuery::IsMouseButtonPressed(MouseButton button) const {
    return mouse_ && mouse_->IsButtonPressed(button);
}

bool InputQuery::IsMouseButtonTriggered(MouseButton button) const {
    return mouse_ && mouse_->IsButtonTriggered(button);
}

bool InputQuery::IsMouseButtonReleased(MouseButton button) const {
    return mouse_ && mouse_->IsButtonReleased(button);
}

int InputQuery::GetMouseDragX() const {
    return mouse_ ? mouse_->GetDragX() : 0;
}

int InputQuery::GetMouseDragY() const {
    return mouse_ ? mouse_->GetDragY() : 0;
}

int InputQuery::GetWheelDelta() const {
    return mouse_ ? mouse_->GetWheelDelta() : 0;
}

POINT InputQuery::GetCursorPosition() const {
    return mouse_ ? mouse_->GetCursorPosition() : POINT{ 0, 0 };
}

// ─── ゲームパッド直接アクセス ─────────────────────────────────

bool InputQuery::IsGamepadConnected() const {
    return gamepad_ && gamepad_->IsConnected();
}

Stick InputQuery::GetLeftStick() const {
    return (gamepad_ && gamepad_->IsConnected()) ? gamepad_->GetLeftStick() : Stick{ 0.0f, 0.0f };
}

Stick InputQuery::GetRightStick() const {
    return (gamepad_ && gamepad_->IsConnected()) ? gamepad_->GetRightStick() : Stick{ 0.0f, 0.0f };
}

float InputQuery::GetLeftTrigger() const {
    return (gamepad_ && gamepad_->IsConnected()) ? gamepad_->GetLeftTrigger() : 0.0f;
}

float InputQuery::GetRightTrigger() const {
    return (gamepad_ && gamepad_->IsConnected()) ? gamepad_->GetRightTrigger() : 0.0f;
}

// ─── キーコンフィグ用：任意の入力を検出 ──────────────────────

std::optional<InputBinding> InputQuery::DetectAnyInput() const {
    // キーボード検出（主要キーをスキャン）
    if (keyboard_) {
        static constexpr uint8_t kScanKeys[] = {
            DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G, DIK_H,
            DIK_I, DIK_J, DIK_K, DIK_L, DIK_M, DIK_N, DIK_O, DIK_P,
            DIK_Q, DIK_R, DIK_S, DIK_T, DIK_U, DIK_V, DIK_W, DIK_X,
            DIK_Y, DIK_Z,
            DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9, DIK_0,
            DIK_F1, DIK_F2, DIK_F3, DIK_F4, DIK_F5, DIK_F6,
            DIK_F7, DIK_F8, DIK_F9, DIK_F10, DIK_F11, DIK_F12,
            DIK_SPACE, DIK_RETURN, DIK_ESCAPE, DIK_BACK, DIK_TAB,
            DIK_LSHIFT, DIK_RSHIFT, DIK_LCONTROL, DIK_RCONTROL, DIK_LALT, DIK_RALT,
            DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT,
            DIK_DELETE, DIK_INSERT, DIK_HOME, DIK_END, DIK_PRIOR, DIK_NEXT,
        };
        for (uint8_t key : kScanKeys) {
            if (keyboard_->IsKeyTriggered(key)) {
                return InputBinding::FromKey(key);
            }
        }
    }

    // マウスボタン検出
    if (mouse_) {
        static constexpr MouseButton kMouseButtons[] = {
            MouseButton::Left, MouseButton::Right, MouseButton::Middle,
            MouseButton::XButton1, MouseButton::XButton2,
        };
        for (MouseButton btn : kMouseButtons) {
            if (mouse_->IsButtonTriggered(btn)) {
                return InputBinding::FromMouseButton(btn);
            }
        }
    }

    // ゲームパッドボタン検出
    if (gamepad_ && gamepad_->IsConnected()) {
        static constexpr GamepadButton kGamepadButtons[] = {
            GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y,
            GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft, GamepadButton::DPadRight,
            GamepadButton::Start, GamepadButton::Back,
            GamepadButton::LeftThumb, GamepadButton::RightThumb,
            GamepadButton::LeftShoulder, GamepadButton::RightShoulder,
        };
        for (GamepadButton btn : kGamepadButtons) {
            if (gamepad_->IsButtonTriggered(btn)) {
                return InputBinding::FromGamepadButton(btn);
            }
        }
    }

    return std::nullopt;
}

// ─── バインディング評価（内部） ───────────────────────────────

bool InputQuery::EvaluatePressed(const InputBinding& b) const {
    switch (b.type) {
    case BindingType::Keyboard:
        return keyboard_ && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return mouse_ && mouse_->IsButtonPressed(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsButtonPressed(static_cast<GamepadButton>(b.code));
    case BindingType::GamepadAxis:
        return EvaluateAxis(b) > 0.1f;
    }
    return false;
}

bool InputQuery::EvaluateTriggered(const InputBinding& b) const {
    switch (b.type) {
    case BindingType::Keyboard:
        return keyboard_ && keyboard_->IsKeyTriggered(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return mouse_ && mouse_->IsButtonTriggered(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsButtonTriggered(static_cast<GamepadButton>(b.code));
    case BindingType::GamepadAxis:
        return EvaluateAxis(b) > 0.1f;
    }
    return false;
}

bool InputQuery::EvaluateReleased(const InputBinding& b) const {
    switch (b.type) {
    case BindingType::Keyboard:
        return keyboard_ && keyboard_->IsKeyReleased(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return mouse_ && mouse_->IsButtonReleased(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsButtonReleased(static_cast<GamepadButton>(b.code));
    case BindingType::GamepadAxis:
        return false;
    }
    return false;
}

float InputQuery::EvaluateAxis(const InputBinding& b) const {
    switch (b.type) {
    case BindingType::Keyboard:
        return (keyboard_ && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code))) ? 1.0f : 0.0f;
    case BindingType::MouseButton:
        return (mouse_ && mouse_->IsButtonPressed(static_cast<MouseButton>(b.code))) ? 1.0f : 0.0f;
    case BindingType::GamepadButton:
        return (gamepad_ && gamepad_->IsConnected() &&
                gamepad_->IsButtonPressed(static_cast<GamepadButton>(b.code))) ? 1.0f : 0.0f;
    case BindingType::GamepadAxis: {
        if (!gamepad_ || !gamepad_->IsConnected()) return 0.0f;
        float val = 0.0f;
        switch (static_cast<GamepadAxis>(b.code)) {
        case GamepadAxis::LeftStickX:  val = gamepad_->GetLeftStick().x;  break;
        case GamepadAxis::LeftStickY:  val = gamepad_->GetLeftStick().y;  break;
        case GamepadAxis::RightStickX: val = gamepad_->GetRightStick().x; break;
        case GamepadAxis::RightStickY: val = gamepad_->GetRightStick().y; break;
        case GamepadAxis::LeftTrigger: val = gamepad_->GetLeftTrigger();  break;
        case GamepadAxis::RightTrigger:val = gamepad_->GetRightTrigger(); break;
        }
        const float signedVal = val * b.axisSign;
        return signedVal > 0.0f ? signedVal : 0.0f;
    }
    }
    return 0.0f;
}

} // namespace CoreEngine
