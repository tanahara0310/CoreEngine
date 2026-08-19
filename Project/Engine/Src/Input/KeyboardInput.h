#pragma once
#include "IDirectInputDevice.h"
#include <wrl/client.h>
#include <cstdint>

namespace CoreEngine
{
/// @brief キーボード入力クラス
class KeyboardInput : public IDirectInputDevice {
public:
    /// @brief 初期化
    /// @param directInput DirectInputオブジェクト
    /// @param hwnd ウィンドウハンドル
    void Initialize(IDirectInput8* directInput, HWND hwnd) override;

    /// @brief 更新処理
    void Update() override;

    /// @brief キーが押されてるかどうか
    /// @param keyNumber キー番号 (0-255)
    bool IsKeyPressed(uint8_t keyNumber) const;

    /// @brief キーが押された瞬間かどうか
    /// @param keyNumber キー番号
    bool IsKeyTriggered(uint8_t keyNumber) const;

    /// @brief キーが離された瞬間かどうか
    /// @param keyNumber キー番号
    bool IsKeyReleased(uint8_t keyNumber) const;

private:
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_; ///< キーボードデバイス
    BYTE key_[256] = {}; ///< 現在のキーの状態
    BYTE preKey_[256] = {}; ///< 1フレーム前のキーの状態
};
}
