#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

/// @brief デバイス基底クラス

namespace CoreEngine
{
class IInputDevice {
public:
    virtual ~IInputDevice() = default;
    virtual void Initialize(IDirectInput8* directInput, HWND hwnd) = 0;
    virtual void Update() = 0;
};
}
