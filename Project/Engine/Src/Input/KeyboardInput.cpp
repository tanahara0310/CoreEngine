#include "pch.h"
#include "KeyboardInput.h"

#include <algorithm> // std::copy を使用するために追加
#include <cassert>


#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
void KeyboardInput::Initialize(IDirectInput8* directInput, HWND hwnd)
{
    HRESULT result;

    // キーボードデバイスの生成
    result = directInput->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(), NULL);
    assert(SUCCEEDED(result));

    // 入力データ形式のセット
    result = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));

    // 排他制御レベルのセット
    result = keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(result));
}

void KeyboardInput::Update()
{
    // 前のキーの状態を保存
    std::copy(std::begin(key_), std::end(key_), std::begin(preKey_));

    // キーボードの状態を取得開始
    HRESULT hr = keyboard_->Acquire();
    if (hr == DIERR_INPUTLOST) {
        hr = keyboard_->Acquire();
    }

    // キーボードの状態を取得
    if (SUCCEEDED(hr)) {
        hr = keyboard_->GetDeviceState(sizeof(key_), key_);
    }

    // 取り込めなかったときに前の状態が残ると、押しっぱなしとして扱われる
    if (FAILED(hr)) {
        std::fill(std::begin(key_), std::end(key_), static_cast<BYTE>(0));
    }
}

void KeyboardInput::Reset()
{
    std::copy(std::begin(key_), std::end(key_), std::begin(preKey_));
    std::fill(std::begin(key_), std::end(key_), static_cast<BYTE>(0));
}

bool KeyboardInput::IsKeyPressed(uint8_t keyNumber) const
{
    return (key_[keyNumber] & 0x80);
}

bool KeyboardInput::IsKeyTriggered(uint8_t keyNumber) const
{
    return (key_[keyNumber] & 0x80) && !(preKey_[keyNumber] & 0x80);
}

bool KeyboardInput::IsKeyReleased(uint8_t keyNumber) const
{
    return !(key_[keyNumber] & 0x80) && (preKey_[keyNumber] & 0x80);
}
}
