#include "pch.h"
#include "GamepadInput.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace CoreEngine
{
namespace
{
    // 振動の 1 回の効き目と、同じ強さを送り直す間隔 [ms]
    constexpr uint32_t kRumbleDurationMs = 1000;
    constexpr uint64_t kRumbleRefreshMs = 500;

    // SDL のボタンと、エンジンのボタンのビット
    struct ButtonMap {
        SDL_GamepadButton sdl;
        GamepadButton button;
    };
    constexpr ButtonMap kButtonMap[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, GamepadButton::A },
        { SDL_GAMEPAD_BUTTON_EAST, GamepadButton::B },
        { SDL_GAMEPAD_BUTTON_WEST, GamepadButton::X },
        { SDL_GAMEPAD_BUTTON_NORTH, GamepadButton::Y },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, GamepadButton::DPadUp },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, GamepadButton::DPadDown },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, GamepadButton::DPadLeft },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, GamepadButton::DPadRight },
        { SDL_GAMEPAD_BUTTON_START, GamepadButton::Start },
        { SDL_GAMEPAD_BUTTON_BACK, GamepadButton::Back },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, GamepadButton::LeftThumb },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, GamepadButton::RightThumb },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GamepadButton::LeftShoulder },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GamepadButton::RightShoulder },
    };

    float NormalizeStickValue(int16_t value, float deadZone)
    {
        const float MAX_VALUE = 32767.0f; // スティックの最大値

        if (std::fabs(static_cast<float>(value)) < deadZone) {
            return 0.0f; // デッドゾーン内は0にする
        }

        // デッドゾーンを超えた値を -1〜1 の範囲に正規化
        return std::clamp(static_cast<float>(value) / MAX_VALUE, -1.0f, 1.0f);
    }

    // SDL の Y 軸（下が正）を上が正へ直す
    int16_t FlipY(int16_t value)
    {
        return static_cast<int16_t>(std::clamp(-static_cast<int>(value), -32768, 32767));
    }

    float TriggerValue(int16_t value)
    {
        return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
    }
}

void GamepadInput::Initialize(DWORD playerIndex)
{
    padIndex_ = playerIndex;
    state_ = {};
    prevState_ = {};
}

void GamepadInput::Attach(SDL_Gamepad* gamepad)
{
    gamepad_ = gamepad;
    canRumble_ = SDL_GetBooleanProperty(SDL_GetGamepadProperties(gamepad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    sentLeftMotor_ = 0;
    sentRightMotor_ = 0;
    SDL_SetGamepadPlayerIndex(gamepad, static_cast<int>(padIndex_));
    SendVibration();
}

SDL_Gamepad* GamepadInput::Detach()
{
    SDL_Gamepad* const gamepad = gamepad_;
    if (gamepad && (sentLeftMotor_ != 0 || sentRightMotor_ != 0)) {
        SDL_RumbleGamepad(gamepad, 0, 0, 0);
    }
    gamepad_ = nullptr;
    isConnected_ = false;
    canRumble_ = false;
    sentLeftMotor_ = 0;
    sentRightMotor_ = 0;
    return gamepad;
}

void GamepadInput::Update()
{
    prevState_ = state_; // 前フレームの状態を保存
    state_ = {};

    // 接続状態を更新
    isConnected_ = gamepad_ && SDL_GamepadConnected(gamepad_);
    if (!isConnected_) {
        return;
    }

    for (const ButtonMap& map : kButtonMap) {
        if (SDL_GetGamepadButton(gamepad_, map.sdl)) {
            state_.buttons |= static_cast<uint16_t>(map.button);
        }
    }
    state_.leftX = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
    state_.leftY = FlipY(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
    state_.rightX = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX);
    state_.rightY = FlipY(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
    state_.leftTrigger = TriggerValue(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    state_.rightTrigger = TriggerValue(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
}

void GamepadInput::Reset()
{
    // 接続しているかは変わらないので触らない
    prevState_ = state_;
    state_ = {};
}

bool GamepadInput::IsConnected() const
{
    return isConnected_;
}

/// ボタンが押されているかどうかをチェック
bool GamepadInput::IsButtonPressed(GamepadButton button) const
{
    return (state_.buttons & static_cast<uint16_t>(button)) != 0;
}

/// ボタンが押されているかどうかをチェック
bool GamepadInput::IsButtonTriggered(GamepadButton button) const
{
    // 今フレームで押されていて、前フレームでは押されていない
    bool now = (state_.buttons & static_cast<uint16_t>(button)) != 0;
    bool prev = (prevState_.buttons & static_cast<uint16_t>(button)) != 0;
    return now && !prev;
}

/// ボタンが離された瞬間かどうかをチェック
bool GamepadInput::IsButtonReleased(GamepadButton button) const
{
    // 今フレームで押されていなくて、前フレームでは押されていた
    bool now = (state_.buttons & static_cast<uint16_t>(button)) == 0;
    bool prev = (prevState_.buttons & static_cast<uint16_t>(button)) != 0;
    return now && prev;
}

Stick GamepadInput::GetLeftStick(float deadZone) const
{
    Stick stick{};

    // 左スティックのデッドゾーンを適用
    stick.x = NormalizeStickValue(state_.leftX, deadZone);
    stick.y = NormalizeStickValue(state_.leftY, deadZone);

    return stick;
}

Stick GamepadInput::GetRightStick(float deadZone) const
{
    Stick stick{};

    // 右スティックのデッドゾーンを適用
    stick.x = NormalizeStickValue(state_.rightX, deadZone);
    stick.y = NormalizeStickValue(state_.rightY, deadZone);

    return stick;
}

float GamepadInput::GetAxisValue(GamepadAxis axis) const
{
    switch (axis) {
    case GamepadAxis::LeftStickX:  return GetLeftStick().x;
    case GamepadAxis::LeftStickY:  return GetLeftStick().y;
    case GamepadAxis::RightStickX: return GetRightStick().x;
    case GamepadAxis::RightStickY: return GetRightStick().y;
    case GamepadAxis::LeftTrigger: return GetLeftTrigger();
    case GamepadAxis::RightTrigger:return GetRightTrigger();
    }
    return 0.0f;
}

float GamepadInput::GetPreviousAxisValue(GamepadAxis axis) const
{
    switch (axis) {
    case GamepadAxis::LeftStickX:
        return NormalizeStickValue(prevState_.leftX, kLeftStickDeadZone);
    case GamepadAxis::LeftStickY:
        return NormalizeStickValue(prevState_.leftY, kLeftStickDeadZone);
    case GamepadAxis::RightStickX:
        return NormalizeStickValue(prevState_.rightX, kRightStickDeadZone);
    case GamepadAxis::RightStickY:
        return NormalizeStickValue(prevState_.rightY, kRightStickDeadZone);
    case GamepadAxis::LeftTrigger:
        return prevState_.leftTrigger;
    case GamepadAxis::RightTrigger:
        return prevState_.rightTrigger;
    }
    return 0.0f;
}

bool GamepadInput::IsAxisTriggered(GamepadAxis axis, bool positive, float threshold) const
{
    const float sign = positive ? 1.0f : -1.0f;
    const float current = GetAxisValue(axis) * sign;
    const float previous = GetPreviousAxisValue(axis) * sign;
    return current > threshold && previous <= threshold;
}

bool GamepadInput::IsAxisReleased(GamepadAxis axis, bool positive, float threshold) const
{
    const float sign = positive ? 1.0f : -1.0f;
    const float current = GetAxisValue(axis) * sign;
    const float previous = GetPreviousAxisValue(axis) * sign;
    return current <= threshold && previous > threshold;
}

void GamepadInput::SetVibration(float leftMotorRatio, float rightMotorRatio)
{
    // 0.0～1.0の範囲にクランプ
    vibrationLeft_ = std::clamp(leftMotorRatio, 0.0f, 1.0f);
    vibrationRight_ = std::clamp(rightMotorRatio, 0.0f, 1.0f);
    SendVibration();
}

void GamepadInput::SetVibrationAllowed(bool allowed)
{
    vibrationAllowed_ = allowed;
    SendVibration();
}

void GamepadInput::SendVibration()
{
    if (!gamepad_ || !canRumble_) {
        return;
    }

    // 0～65535に変換（止められている間は 0）
    const uint16_t leftMotor = vibrationAllowed_ ? static_cast<uint16_t>(vibrationLeft_ * 65535.0f) : 0;
    const uint16_t rightMotor = vibrationAllowed_ ? static_cast<uint16_t>(vibrationRight_ * 65535.0f) : 0;
    const bool on = leftMotor != 0 || rightMotor != 0;
    const bool changed = leftMotor != sentLeftMotor_ || rightMotor != sentRightMotor_;
    const uint64_t now = SDL_GetTicks();
    if (!changed && (!on || now - sentAtMs_ < kRumbleRefreshMs)) {
        return;
    }

    if (SDL_RumbleGamepad(gamepad_, leftMotor, rightMotor, on ? kRumbleDurationMs : 0)) {
        sentLeftMotor_ = leftMotor;
        sentRightMotor_ = rightMotor;
        sentAtMs_ = now;
    }
}

float GamepadInput::GetLeftTrigger() const
{
    return state_.leftTrigger;
}

float GamepadInput::GetRightTrigger() const
{
    return state_.rightTrigger;
}
}
