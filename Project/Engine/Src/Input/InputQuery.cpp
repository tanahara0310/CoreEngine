#include "pch.h"
#include "InputQuery.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "GamepadInput.h"
#include <cmath>

namespace CoreEngine {

namespace {
    /// @brief 組み合わせに使うキーか（これ自体も単体で割り当てられる）
    bool IsModifierKey(uint8_t code) {
        return code == DIK_LCONTROL || code == DIK_RCONTROL
            || code == DIK_LSHIFT || code == DIK_RSHIFT
            || code == DIK_LALT || code == DIK_RALT;
    }
}

void InputQuery::Initialize(KeyboardInput* keyboard, MouseInput* mouse, GamepadInput* gamepad) {
    keyboard_ = keyboard;
    mouse_    = mouse;
    gamepad_  = gamepad;

    // 既定値を組んでから、保存済みのキーコンフィグがあれば重ねる。
    // ファイルが無い / 壊れている場合は LoadFromFile が false を返し、既定値のまま動く
    config_.ResetToDefault();
    config_.LoadFromFile(std::string(InputConfig::kDefaultFilePath));
}

// ─── アクションベース問い合わせ ───────────────────────────────

bool InputQuery::IsActionActive(InputAction action) const {
    return Overlaps(InputActionToContexts(action), activeContexts_);
}

bool InputQuery::IsActionPressed(InputAction action) const {
    if (!IsActionActive(action)) return false;
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluatePressed(b)) return true;
    }
    return false;
}

bool InputQuery::IsActionTriggered(InputAction action) const {
    if (!IsActionActive(action)) return false;
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateTriggered(b)) return true;
    }
    return false;
}

bool InputQuery::IsActionReleased(InputAction action) const {
    // 場面が切り替わった瞬間に「離した」を取りこぼさないよう、ここだけは場面を見ない。
    // 押している途中でメニューへ移っても、指を離せば押し下げの後始末が回る
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateReleased(b)) return true;
    }
    return false;
}

float InputQuery::GetAxisValue(InputAction action) const {
    if (!IsActionActive(action)) return 0.0f;
    float maxVal = 0.0f;
    for (const auto& b : config_.GetBindings(action)) {
        const float val = EvaluateAxis(b);
        if (val > maxVal) maxVal = val;
    }
    return maxVal;
}

float InputQuery::GetAxis(InputAction negative, InputAction positive) const {
    return GetAxisValue(positive) - GetAxisValue(negative);
}

Vector2 InputQuery::GetAxis2D(InputAction negativeX, InputAction positiveX,
                              InputAction negativeY, InputAction positiveY) const {
    Vector2 value{ GetAxis(negativeX, positiveX), GetAxis(negativeY, positiveY) };
    // キーボードで斜めに入れると長さが √2 になる。スティックと速さを揃えるため丸める
    const float lengthSquared = value.x * value.x + value.y * value.y;
    if (lengthSquared > 1.0f) {
        const float inverse = 1.0f / std::sqrt(lengthSquared);
        value.x *= inverse;
        value.y *= inverse;
    }
    return value;
}

// ─── キーボード直接アクセス ───────────────────────────────────

bool InputQuery::IsKeyPressed(uint8_t dikCode) const {
    return !keyboardSuppressed_ && keyboard_ && keyboard_->IsKeyPressed(dikCode);
}

bool InputQuery::IsKeyTriggered(uint8_t dikCode) const {
    return !keyboardSuppressed_ && keyboard_ && keyboard_->IsKeyTriggered(dikCode);
}

bool InputQuery::IsKeyReleased(uint8_t dikCode) const {
    // 離した瞬間だけは外さない。読まない設定に変わった途端、押し下げが宙に浮くのを防ぐ
    return keyboard_ && keyboard_->IsKeyReleased(dikCode);
}

bool InputQuery::IsKeyTriggeredRaw(uint8_t dikCode) const {
    return keyboard_ && keyboard_->IsKeyTriggered(dikCode);
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

void InputQuery::SetVibration(float leftMotorRatio, float rightMotorRatio) {
    if (gamepad_ && gamepad_->IsConnected()) {
        gamepad_->SetVibration(leftMotorRatio, rightMotorRatio);
    }
}

// ─── キーコンフィグ用：任意の入力を検出 ──────────────────────

std::optional<InputBinding> InputQuery::DetectAnyInput() const {
    // 一緒に押している修飾キー（Ctrl+S のような組み合わせを作るため）
    const InputModifier modifiers = CurrentModifiers();

    // キーボード検出（使える全キー。名前の無いキーは "Key:0x.." で保存される）
    if (keyboard_) {
        // 修飾キー単体も割り当てられるよう、まず修飾キー以外を探す
        for (int pass = 0; pass < 2; ++pass) {
            const bool wantModifier = (pass == 1);
            for (int code = 1; code < 256; ++code) {
                const auto key = static_cast<uint8_t>(code);
                if (IsModifierKey(key) != wantModifier) {
                    continue;
                }
                if (keyboard_->IsKeyTriggered(key)) {
                    InputBinding binding = InputBinding::FromKey(key);
                    if (!wantModifier) {
                        binding.modifiers = modifiers;
                    }
                    return binding;
                }
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
                InputBinding binding = InputBinding::FromMouseButton(btn);
                binding.modifiers = modifiers;
                return binding;
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

        // ゲームパッドのアナログ軸検出（スティック / トリガー）
        // ボタンのような「押した瞬間」が取れないので、倒し込み量のしきい値で拾う。
        // 遊び程度の傾きを誤って割り当てないよう、デッドゾーンより十分深い位置に敷居を置く
        constexpr float kAxisDetectThreshold = 0.6f;
        const Stick leftStick  = gamepad_->GetLeftStick();
        const Stick rightStick = gamepad_->GetRightStick();
        const struct { GamepadAxis axis; float value; } kAxes[] = {
            { GamepadAxis::LeftStickX,   leftStick.x                 },
            { GamepadAxis::LeftStickY,   leftStick.y                 },
            { GamepadAxis::RightStickX,  rightStick.x                },
            { GamepadAxis::RightStickY,  rightStick.y                },
            { GamepadAxis::LeftTrigger,  gamepad_->GetLeftTrigger()  },
            { GamepadAxis::RightTrigger, gamepad_->GetRightTrigger() },
        };
        for (const auto& [axis, value] : kAxes) {
            if (std::fabs(value) >= kAxisDetectThreshold) {
                return InputBinding::FromGamepadAxis(axis, value > 0.0f);
            }
        }
    }

    return std::nullopt;
}

// ─── バインディング評価（内部） ───────────────────────────────

bool InputQuery::ModifiersHeld(InputModifier modifiers) const {
    if (modifiers == InputModifier::None) {
        return true;
    }
    if (!keyboard_ || keyboardSuppressed_) {
        return false;
    }
    if (HasModifier(modifiers, InputModifier::Ctrl)
        && !keyboard_->IsKeyPressed(DIK_LCONTROL) && !keyboard_->IsKeyPressed(DIK_RCONTROL)) {
        return false;
    }
    if (HasModifier(modifiers, InputModifier::Shift)
        && !keyboard_->IsKeyPressed(DIK_LSHIFT) && !keyboard_->IsKeyPressed(DIK_RSHIFT)) {
        return false;
    }
    if (HasModifier(modifiers, InputModifier::Alt)
        && !keyboard_->IsKeyPressed(DIK_LALT) && !keyboard_->IsKeyPressed(DIK_RALT)) {
        return false;
    }
    return true;
}

InputModifier InputQuery::CurrentModifiers() const {
    InputModifier modifiers = InputModifier::None;
    if (!keyboard_) {
        return modifiers;
    }
    if (keyboard_->IsKeyPressed(DIK_LCONTROL) || keyboard_->IsKeyPressed(DIK_RCONTROL)) {
        modifiers |= InputModifier::Ctrl;
    }
    if (keyboard_->IsKeyPressed(DIK_LSHIFT) || keyboard_->IsKeyPressed(DIK_RSHIFT)) {
        modifiers |= InputModifier::Shift;
    }
    if (keyboard_->IsKeyPressed(DIK_LALT) || keyboard_->IsKeyPressed(DIK_RALT)) {
        modifiers |= InputModifier::Alt;
    }
    return modifiers;
}

bool InputQuery::EvaluatePressed(const InputBinding& b) const {
    if (!ModifiersHeld(b.modifiers)) return false;
    switch (b.type) {
    case BindingType::Keyboard:
        return !keyboardSuppressed_ && keyboard_ && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code));
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
    if (!ModifiersHeld(b.modifiers)) return false;
    switch (b.type) {
    case BindingType::Keyboard:
        return !keyboardSuppressed_ && keyboard_ && keyboard_->IsKeyTriggered(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return mouse_ && mouse_->IsButtonTriggered(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsButtonTriggered(static_cast<GamepadButton>(b.code));
    case BindingType::GamepadAxis:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsAxisTriggered(
                   static_cast<GamepadAxis>(b.code), b.axisSign >= 0.0f);
    }
    return false;
}

bool InputQuery::EvaluateReleased(const InputBinding& b) const {
    // 離した瞬間は修飾キーを見ない。Ctrl を先に離しても押し下げの後始末が回るように
    switch (b.type) {
    case BindingType::Keyboard:
        return keyboard_ && keyboard_->IsKeyReleased(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return mouse_ && mouse_->IsButtonReleased(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsButtonReleased(static_cast<GamepadButton>(b.code));
    case BindingType::GamepadAxis:
        return gamepad_ && gamepad_->IsConnected() &&
               gamepad_->IsAxisReleased(
                   static_cast<GamepadAxis>(b.code), b.axisSign >= 0.0f);
    }
    return false;
}

float InputQuery::EvaluateAxis(const InputBinding& b) const {
    if (!ModifiersHeld(b.modifiers)) return 0.0f;
    switch (b.type) {
    case BindingType::Keyboard:
        return (!keyboardSuppressed_ && keyboard_ && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code)))
            ? 1.0f : 0.0f;
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
