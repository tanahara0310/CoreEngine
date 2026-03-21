#include "MouseInput.h"
#include <cassert>
#include <corecrt_wstdio.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
    void MouseInput::Initialize(IDirectInput8* directInput, HWND hwnd)
    {
        HRESULT result;

        // ウィンドウハンドルを保存
        hwnd_ = hwnd;

        // マウスデバイスの初期化
        result = directInput->CreateDevice(GUID_SysMouse, &mouse_, NULL);
        assert(SUCCEEDED(result));

        // マウスデバイスの設定
        result = mouse_->SetDataFormat(&c_dfDIMouse2);
        assert(SUCCEEDED(result));

        // マウスの入力モードを設定
        result = mouse_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        assert(SUCCEEDED(result));

    }

    void MouseInput::Update()
    {
        prevMouseState_ = mouseState_;

        // 相対移動量を事前にリセット
        // GetDeviceState が失敗した場合や Acquire できない場合でも
        // 前フレームの値 (lX/lY/lZ) が残存してカメラが誤作動するのを防ぐ
        mouseState_.lX = 0;
        mouseState_.lY = 0;
        mouseState_.lZ = 0;

        // マウスの状態を取得
        HRESULT hr = mouse_->Acquire();
        if (hr == DIERR_INPUTLOST) {
            // デバイスロストなら再取得をする
            hr = mouse_->Acquire();
        }

        // DIERR_NOTACQUIRED (非アクティブ) や DIERR_OTHERAPPHASPRIO (他アプリ優先) は無視

        // 成功／再取得できた場合だけ状態取得
        if (SUCCEEDED(hr)) {
            hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_);
            if (FAILED(hr)) {
                // 取得失敗時も相対移動量は 0 のまま維持（上でリセット済み）
                OutputDebugStringW(L"Mouse GetDeviceState に失敗しました\n");
            }
        }
    }

    // マウスボタンが押されているかどうか
    bool MouseInput::IsButtonPressed(MouseButton button) const
    {
        int idx = static_cast<int>(button);
        return (mouseState_.rgbButtons[idx] & 0x80);
    }

    // マウスボタンが押された瞬間かどうか
    bool MouseInput::IsButtonTriggered(MouseButton button) const
    {
        int idx = static_cast<int>(button);
        return (mouseState_.rgbButtons[idx] & 0x80) && !(prevMouseState_.rgbButtons[idx] & 0x80);
    }

    // マウスボタンが離されたかどうか
    bool MouseInput::IsButtonReleased(MouseButton button) const
    {
        int idx = static_cast<int>(button);
        return !(mouseState_.rgbButtons[idx] & 0x80) && (prevMouseState_.rgbButtons[idx] & 0x80);
    }

    // マウスホイールの回転量を取得
    int MouseInput::GetWheelDelta() const
    {
        // ホイールの回転量を取得
        return mouseState_.lZ;
    }

    // マウスドラッグ量のX成分を取得
    int MouseInput::GetDragX() const
    {
        // マウスのX軸の移動量を取得(右へ正、左へ負)
        return mouseState_.lX;
    }

    // マウスドラッグ量のY成分を取得
    int MouseInput::GetDragY() const
    {
        // マウスのY軸の移動量を取得(下へ正、上へ負)
        return mouseState_.lY;
    }

    POINT MouseInput::GetCursorPosition() const
    {
        POINT pos{};

        // マウスのカーソル位置を取得
        if (GetCursorPos(&pos)) {

            // ウィンドウのクライアント座標に変換
            ScreenToClient(hwnd_, &pos);

            return pos;
        }

        // 取得失敗時は座標を(0, 0)にする
        return { 0, 0 };
    }
}
