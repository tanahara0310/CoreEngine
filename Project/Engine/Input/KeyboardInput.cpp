#include "KeyboardInput.h"

#include <algorithm> // std::copy を使用するために追加
#include <cassert>


#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
struct KeyName {
    uint8_t code;
    const char* name;
};

static const KeyName kKeyNameTable[] = {
    { DIK_A, "A" },
    { DIK_B, "B" },
    { DIK_C, "C" },
    { DIK_D, "D" },
    { DIK_E, "E" },
    { DIK_F, "F" },
    { DIK_G, "G" },
    { DIK_H, "H" },
    { DIK_I, "I" },
    { DIK_J, "J" },
    { DIK_K, "K" },
    { DIK_L, "L" },
    { DIK_M, "M" },
    { DIK_N, "N" },
    { DIK_O, "O" },
    { DIK_P, "P" },
    { DIK_Q, "Q" },
    { DIK_R, "R" },
    { DIK_S, "S" },
    { DIK_T, "T" },
    { DIK_U, "U" },
    { DIK_V, "V" },
    { DIK_W, "W" },
    { DIK_X, "X" },
    { DIK_Y, "Y" },
    { DIK_Z, "Z" },
    { DIK_SPACE, "Space" },
    { DIK_RETURN, "Enter" },
    { DIK_ESCAPE, "Escape" },
    { DIK_UP, "Up" },
    { DIK_DOWN, "Down" },
    { DIK_LEFT, "Left" },
    { DIK_RIGHT, "Right" },
    // 必要なら増やす
};

void KeyboardInput::Initialize(IDirectInput8* directInput, HWND hwnd)
{
    HRESULT result;

    // キーボードデバイスの生成
    result = directInput->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
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
    keyboard_->Acquire();

    // キーボードの状態を取得
    keyboard_->GetDeviceState(sizeof(key_), key_);

    // どのキーが押されたかを確認(デバッグ用)d
    for (const auto& keyInfo : kKeyNameTable) {
        if (IsKeyTriggered(keyInfo.code)) {
            wchar_t buffer[64];
            swprintf_s(buffer, L"%hs が押されました\n", keyInfo.name); // %hs で char* を wide に
            OutputDebugStringW(buffer);
        }
    }
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
