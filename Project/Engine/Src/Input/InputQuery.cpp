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

void InputQuery::Initialize(KeyboardInput* keyboard, MouseInput* mouse,
                           const std::array<GamepadInput*, kMaxGamepads>& gamepads) {
    keyboard_ = keyboard;
    mouse_    = mouse;
    gamepads_ = gamepads;

    // 既定値を組んでから、保存済みのキーコンフィグがあれば重ねる。
    // ファイルが無い / 壊れている場合は LoadFromFile が false を返し、既定値のまま動く
    config_.ResetToDefault();
    config_.LoadFromFile(std::string(InputConfig::kDefaultFilePath));
}

// ─── アクションベース問い合わせ ───────────────────────────────

bool InputQuery::IsActionActive(InputAction action) const {
    return Overlaps(InputActionToContexts(action), activeContexts_);
}

bool InputQuery::IsActionPressed(InputAction action, int player) const {
    if (!IsActionActive(action)) return false;
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluatePressed(b, player)) return true;
    }
    return false;
}

bool InputQuery::IsActionTriggered(InputAction action, int player) const {
    if (!IsActionActive(action)) return false;
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateTriggered(b, player)) return true;
    }
    return false;
}

bool InputQuery::IsActionReleased(InputAction action, int player) const {
    // 場面が切り替わった瞬間に「離した」を取りこぼさないよう、ここだけは場面を見ない。
    // 押している途中でメニューへ移っても、指を離せば押し下げの後始末が回る
    for (const auto& b : config_.GetBindings(action)) {
        if (EvaluateReleased(b, player)) return true;
    }
    return false;
}

float InputQuery::GetAxisValue(InputAction action, int player) const {
    if (!IsActionActive(action)) return 0.0f;
    float maxVal = 0.0f;
    for (const auto& b : config_.GetBindings(action)) {
        const float val = EvaluateAxis(b, player);
        if (val > maxVal) maxVal = val;
    }
    return maxVal;
}

float InputQuery::GetAxis(InputAction negative, InputAction positive, int player) const {
    return GetAxisValue(positive, player) - GetAxisValue(negative, player);
}

Vector2 InputQuery::GetAxis2D(InputAction negativeX, InputAction positiveX,
                              InputAction negativeY, InputAction positiveY,
                              int player) const {
    Vector2 value{ GetAxis(negativeX, positiveX, player),
                   GetAxis(negativeY, positiveY, player) };
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

GamepadInput* InputQuery::GamepadAt(int player) const {
    if (player < 0 || player >= kMaxGamepads) {
        return nullptr;
    }
    GamepadInput* const pad = gamepads_[static_cast<std::size_t>(player)];
    return (pad && pad->IsConnected()) ? pad : nullptr;
}

bool InputQuery::IsGamepadConnected(int player) const {
    return GamepadAt(player) != nullptr;
}

int InputQuery::GetConnectedGamepadCount() const {
    int count = 0;
    for (GamepadInput* const pad : gamepads_) {
        if (pad && pad->IsConnected()) {
            ++count;
        }
    }
    return count;
}

Stick InputQuery::GetLeftStick(int player) const {
    GamepadInput* const pad = GamepadAt(player);
    return pad ? pad->GetLeftStick() : Stick{ 0.0f, 0.0f };
}

Stick InputQuery::GetRightStick(int player) const {
    GamepadInput* const pad = GamepadAt(player);
    return pad ? pad->GetRightStick() : Stick{ 0.0f, 0.0f };
}

float InputQuery::GetLeftTrigger(int player) const {
    GamepadInput* const pad = GamepadAt(player);
    return pad ? pad->GetLeftTrigger() : 0.0f;
}

float InputQuery::GetRightTrigger(int player) const {
    GamepadInput* const pad = GamepadAt(player);
    return pad ? pad->GetRightTrigger() : 0.0f;
}

void InputQuery::SetVibration(float leftMotorRatio, float rightMotorRatio, int player) {
    if (GamepadInput* const pad = GamepadAt(player)) {
        pad->SetVibration(leftMotorRatio, rightMotorRatio);
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

    // ゲームパッドボタン検出（どのパッドで押しても割り当てられる）
    for (GamepadInput* const pad : gamepads_) {
        if (!pad || !pad->IsConnected()) {
            continue;
        }
        static constexpr GamepadButton kGamepadButtons[] = {
            GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y,
            GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft, GamepadButton::DPadRight,
            GamepadButton::Start, GamepadButton::Back,
            GamepadButton::LeftThumb, GamepadButton::RightThumb,
            GamepadButton::LeftShoulder, GamepadButton::RightShoulder,
        };
        for (GamepadButton btn : kGamepadButtons) {
            if (pad->IsButtonTriggered(btn)) {
                InputBinding binding = InputBinding::FromGamepadButton(btn);
                binding.modifiers = modifiers;
                return binding;
            }
        }

        // ゲームパッドのアナログ軸検出（スティック / トリガー）
        // ボタンのような「押した瞬間」が取れないので、倒し込み量のしきい値で拾う。
        // 遊び程度の傾きを誤って割り当てないよう、デッドゾーンより十分深い位置に敷居を置く
        constexpr float kAxisDetectThreshold = 0.6f;
        const Stick leftStick  = pad->GetLeftStick();
        const Stick rightStick = pad->GetRightStick();
        const struct { GamepadAxis axis; float value; } kAxes[] = {
            { GamepadAxis::LeftStickX,   leftStick.x                 },
            { GamepadAxis::LeftStickY,   leftStick.y                 },
            { GamepadAxis::RightStickX,  rightStick.x                },
            { GamepadAxis::RightStickY,  rightStick.y                },
            { GamepadAxis::LeftTrigger,  pad->GetLeftTrigger()  },
            { GamepadAxis::RightTrigger, pad->GetRightTrigger() },
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

bool InputQuery::EvaluatePressed(const InputBinding& b, int player) const {
    if (!ModifiersHeld(b.modifiers)) return false;
    switch (b.type) {
    case BindingType::Keyboard:
        // パッドを指名して読むときは、キーボードとマウスを混ぜない（2 人目が巻き込まれる）
        return player == kAnyGamepad && !keyboardSuppressed_ && keyboard_
            && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return player == kAnyGamepad && mouse_
            && mouse_->IsButtonPressed(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsButtonPressed(static_cast<GamepadButton>(b.code));
            });
    case BindingType::GamepadAxis:
        return EvaluateAxis(b, player) > 0.1f;
    }
    return false;
}

bool InputQuery::EvaluateTriggered(const InputBinding& b, int player) const {
    if (!ModifiersHeld(b.modifiers)) return false;
    switch (b.type) {
    case BindingType::Keyboard:
        return player == kAnyGamepad && !keyboardSuppressed_ && keyboard_
            && keyboard_->IsKeyTriggered(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return player == kAnyGamepad && mouse_
            && mouse_->IsButtonTriggered(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsButtonTriggered(static_cast<GamepadButton>(b.code));
            });
    case BindingType::GamepadAxis:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsAxisTriggered(static_cast<GamepadAxis>(b.code), b.axisSign >= 0.0f);
            });
    }
    return false;
}

bool InputQuery::EvaluateReleased(const InputBinding& b, int player) const {
    // 離した瞬間は修飾キーを見ない。Ctrl を先に離しても押し下げの後始末が回るように
    switch (b.type) {
    case BindingType::Keyboard:
        return player == kAnyGamepad && keyboard_
            && keyboard_->IsKeyReleased(static_cast<uint8_t>(b.code));
    case BindingType::MouseButton:
        return player == kAnyGamepad && mouse_
            && mouse_->IsButtonReleased(static_cast<MouseButton>(b.code));
    case BindingType::GamepadButton:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsButtonReleased(static_cast<GamepadButton>(b.code));
            });
    case BindingType::GamepadAxis:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsAxisReleased(static_cast<GamepadAxis>(b.code), b.axisSign >= 0.0f);
            });
    }
    return false;
}

float InputQuery::EvaluateAxis(const InputBinding& b, int player) const {
    if (!ModifiersHeld(b.modifiers)) return 0.0f;
    switch (b.type) {
    case BindingType::Keyboard:
        return (player == kAnyGamepad && !keyboardSuppressed_ && keyboard_
                && keyboard_->IsKeyPressed(static_cast<uint8_t>(b.code))) ? 1.0f : 0.0f;
    case BindingType::MouseButton:
        return (player == kAnyGamepad && mouse_
                && mouse_->IsButtonPressed(static_cast<MouseButton>(b.code))) ? 1.0f : 0.0f;
    case BindingType::GamepadButton:
        return AnyGamepad(player, [&](GamepadInput& pad) {
            return pad.IsButtonPressed(static_cast<GamepadButton>(b.code));
            }) ? 1.0f : 0.0f;
    case BindingType::GamepadAxis:
        // 割り当ては向きを持つ（"Axis:LeftStickX-" など）。逆向きに倒していれば 0
        return MaxGamepad(player, [&](GamepadInput& pad) {
            const float value = pad.GetAxisValue(static_cast<GamepadAxis>(b.code)) * b.axisSign;
            return value > 0.0f ? value : 0.0f;
            });
    }
    return 0.0f;
}

} // namespace CoreEngine
