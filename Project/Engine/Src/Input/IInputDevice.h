#pragma once

/// @brief 入力デバイス基底インターフェース
/// Update() のみを共通契約とする。初期化方法はデバイス種別によって異なるため派生インターフェースで定義する

namespace CoreEngine
{
class IInputDevice {
public:
    virtual ~IInputDevice() = default;
    virtual void Update() = 0;
};
}
