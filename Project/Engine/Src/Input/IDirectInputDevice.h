#pragma once
#include "IInputDevice.h"

namespace CoreEngine
{
    /// @brief DirectInput系デバイスの初期化インターフェース
    /// XInput(GamepadInput)はこのインターフェースを継承しない
    class IDirectInputDevice : public IInputDevice {
    public:
        virtual ~IDirectInputDevice() = default;

        /// @brief DirectInputデバイスの初期化
        /// @param directInput DirectInputオブジェクト
        /// @param hwnd ウィンドウハンドル
        virtual void Initialize(IDirectInput8* directInput, HWND hwnd) = 0;
    };
}
