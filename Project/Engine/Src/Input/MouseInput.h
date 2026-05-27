#pragma once
#include "IDirectInputDevice.h"
#include <wrl/client.h>
#include <cstdint>
#include <dinput.h>


namespace CoreEngine
{
enum class MouseButton {
    Left = 0,    ///< 左ボタン
    Right = 1,   ///< 右ボタン
    Middle = 2,  ///< 中央ボタン
    XButton1 = 3,///< Xボタン1
    XButton2 = 4 ///< Xボタン2
};

/// @brief マウス入力クラス
class MouseInput : public IDirectInputDevice {
public:
    /// @brief 初期化
    /// @param directInput DirectInputオブジェクト
    /// @param hwnd ウィンドウハンドル
    void Initialize(IDirectInput8* directInput, HWND hwnd) override;

    /// @brief マウスの状態を更新
    void Update() override;

    /// @brief ボタンが押されているかどうか
    bool IsButtonPressed(MouseButton button) const;

    /// @brief ボタンが押された瞬間かどうか
    bool IsButtonTriggered(MouseButton button) const;

    /// @brief ボタンが離されたかどうか
    bool IsButtonReleased(MouseButton button) const;

    /// @brief マウスホイールの回転量を取得
    int GetWheelDelta() const;

    /// @brief マウスドラッグ量のX成分を取得
    int GetDragX() const;

    /// @brief マウスドラッグ量のY成分を取得
    int GetDragY() const;

    POINT GetCursorPosition() const;

private:
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_; ///< マウスデバイス
    DIMOUSESTATE2 mouseState_ = {};     ///< マウスの状態
    DIMOUSESTATE2 prevMouseState_ = {}; ///< 前回のマウスの状態
    HWND hwnd_ = nullptr;               ///< ウィンドウハンドル
};
}
