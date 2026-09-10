#include "pch.h"
#include "GamepadInput.h"
#include <algorithm> // clamp を使うなら必要
#include <cassert>
#include <corecrt_math.h>

#pragma comment(lib, "XInput.lib")

// 内部ヘルパー

namespace CoreEngine
{
static float NormalizeStickValue(SHORT value, float deadZone)
{

    const float MAX_VALUE = 32767.0f; // XInputのスティックの最大値
    float normalized = static_cast<float>(value);

    if (fabsf(static_cast<float>(value)) < deadZone) {
        return 0.0f; // デッドゾーン内は0にする
    }

    // 正規化
    normalized /= MAX_VALUE;

    // デッドゾーンを超えた値を0〜1の範囲に正規化
    normalized = std::clamp(normalized, -1.0f, 1.0f);

    return normalized;
}

void GamepadInput::Initialize(DWORD playerIndex)
{
    padIndex_ = playerIndex;
    ZeroMemory(&state_, sizeof(XINPUT_STATE));
    ZeroMemory(&prevState_, sizeof(XINPUT_STATE));
}

void GamepadInput::Update()
{
    prevState_ = state_; // 前フレームの状態を保存

    // XInputでゲームパッド0番を取得
    ZeroMemory(&state_, sizeof(XINPUT_STATE));
    DWORD result = XInputGetState(padIndex_, &state_);

    // 接続状態を更新
    isConnected_ = (result == ERROR_SUCCESS);

#ifdef _DEBUG
    // ★★★ 接続状態のデバッグログ（起動時のみ） ★★★
    static bool hasLoggedConnection = false;
    static bool wasConnected = false;

    if (!hasLoggedConnection || (wasConnected != isConnected_)) {
        if (isConnected_) {
            printf("GamepadInput::Update - Controller %d connected successfully\n", padIndex_);
        } else {
            printf("GamepadInput::Update - Controller %d not connected (Error: 0x%08X)\n", padIndex_, result);
        }
        hasLoggedConnection = true;
        wasConnected = isConnected_;
    }
#endif

    // if (result != ERROR_SUCCESS) {
    //     // パッドが接続されていない
    //     OutputDebugStringW(L"ゲームパッドが接続されていません\n");
    // }
}

bool GamepadInput::IsConnected() const
{
    return isConnected_;
}

/// ボタンが押されているかどうかをチェック
bool GamepadInput::IsButtonPressed(GamepadButton button) const
{
    return (state_.Gamepad.wButtons & static_cast<WORD>(button)) != 0;
}

/// ボタンが押されているかどうかをチェック
bool GamepadInput::IsButtonTriggered(GamepadButton button) const
{
    // 今フレームで押されていて、前フレームでは押されていない
    bool now = (state_.Gamepad.wButtons & static_cast<WORD>(button)) != 0;
    bool prev = (prevState_.Gamepad.wButtons & static_cast<WORD>(button)) != 0;
    return now && !prev;
}

/// ボタンが離された瞬間かどうかをチェック
bool GamepadInput::IsButtonReleased(GamepadButton button) const
{
    // 今フレームで押されていなくて、前フレームでは押されていた
    bool now = (state_.Gamepad.wButtons & static_cast<WORD>(button)) == 0;
    bool prev = (prevState_.Gamepad.wButtons & static_cast<WORD>(button)) != 0;
    return now && prev;
}

Stick GamepadInput::GetLeftStick(float deadZone) const
{
    Stick stick{};

    // 左スティックのデッドゾーンを適用
    stick.x = NormalizeStickValue(state_.Gamepad.sThumbLX, deadZone);
    stick.y = NormalizeStickValue(state_.Gamepad.sThumbLY, deadZone);

    return stick;
}

Stick GamepadInput::GetRightStick(float deadZone) const
{
    Stick stick{};

    // 右スティックのデッドゾーンを適用
    stick.x = NormalizeStickValue(state_.Gamepad.sThumbRX, deadZone);
    stick.y = NormalizeStickValue(state_.Gamepad.sThumbRY, deadZone);

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
        return NormalizeStickValue(
            prevState_.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    case GamepadAxis::LeftStickY:
        return NormalizeStickValue(
            prevState_.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    case GamepadAxis::RightStickX:
        return NormalizeStickValue(
            prevState_.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    case GamepadAxis::RightStickY:
        return NormalizeStickValue(
            prevState_.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    case GamepadAxis::LeftTrigger:
        return static_cast<float>(prevState_.Gamepad.bLeftTrigger) / 255.0f;
    case GamepadAxis::RightTrigger:
        return static_cast<float>(prevState_.Gamepad.bRightTrigger) / 255.0f;
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

void GamepadInput::SetVibration(float leftMotorRatio, float rightMotorRatio)
{
    // 0.0～1.0の範囲にクランプ
    leftMotorRatio = std::clamp(leftMotorRatio, 0.0f, 1.0f);
    rightMotorRatio = std::clamp(rightMotorRatio, 0.0f, 1.0f);

    // 0～65535に変換
    WORD leftMotor = static_cast<WORD>(leftMotorRatio * 65535.0f);
    WORD rightMotor = static_cast<WORD>(rightMotorRatio * 65535.0f);

    XINPUT_VIBRATION vibration{};
    ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
    vibration.wLeftMotorSpeed = leftMotor;
    vibration.wRightMotorSpeed = rightMotor;
}

float GamepadInput::GetLeftTrigger() const
{
    // XInputのトリガー値は0〜255の範囲
    const float MAX_TRIGGER_VALUE = 255.0f;
    return static_cast<float>(state_.Gamepad.bLeftTrigger) / MAX_TRIGGER_VALUE;
}

float GamepadInput::GetRightTrigger() const
{
    // XInputのトリガー値は0〜255の範囲
    const float MAX_TRIGGER_VALUE = 255.0f;
    return static_cast<float>(state_.Gamepad.bRightTrigger) / MAX_TRIGGER_VALUE;
}
}
