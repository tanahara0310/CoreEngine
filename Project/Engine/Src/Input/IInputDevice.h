#pragma once

namespace CoreEngine
{
/// @brief 入力デバイス基底インターフェース
/// Update() のみを共通契約とする。初期化方法はデバイス種別によって異なるため派生インターフェースで定義する
class IInputDevice {
public:
    virtual ~IInputDevice() = default;
        /// @brief 1 フレーム分の入力状態を取り込む（InputManager が毎フレーム呼ぶ）
    virtual void Update() = 0;
};
}
