#pragma once
#include "IInputDevice.h"
#include <cstdint>
#include <dinput.h>


namespace CoreEngine
{
enum class MouseButton {
    Left = 0, ///< 左ボタン
    Right = 1, ///< 右ボタン
    Middle = 2, ///< 中央ボタン
    XButton1 = 3, ///< Xボタン1
    XButton2 = 4 ///< Xボタン2
};

/// @brief マウス入力クラス
class MouseInput : public IInputDevice {
public:
    /// @brief 初期化
    /// @param directInput directInput
    /// @param hwnd　ウィンドウハンドル
    void Initialize(IDirectInput8* directInput, HWND hwnd) override;

    /// @brief マウスの状態を更新
    void Update() override;

    /// @brief ボタンが押されているかどうか
    /// @param button 押したいボタン
    /// @return 押されている場合はtrue、そうでない場合はfalse
    bool IsButtonPressed(MouseButton button) const;

    /// @brief ボタンが押された瞬間かどうか
    /// @param button 押したいボタン
    /// @return 押された瞬間の場合はtrue、そうでない場合はfalse
    bool IsButtonTriggered(MouseButton button) const;

    /// @brief ボタンが離されたかどうか
    /// @param button 離したいボタン
    /// @return 離された場合はtrue、そうでない場合はfalse
    bool IsButtonReleased(MouseButton button) const;

    /// @brief マウスホイールの回転量を取得
    /// @return ホイールの回転量
    int GetWheelDelta() const;

    /// @brief マウスドラッグ量のX成分を取得
    /// @return マウスドラッグ量のX成分
    int GetDragX() const;

    /// @brief マウスドラッグ量のY成分を取得
    /// @return マウスドラッグ量のY成分
    int GetDragY() const;

    POINT GetCursorPosition() const;

private:
    IDirectInputDevice8* mouse_ = nullptr; ///< マウスデバイス
    DIMOUSESTATE2 mouseState_ = {}; ///< マウスの状態
    DIMOUSESTATE2 prevMouseState_ = {}; ///< 前回のマウスの状態

    HWND hwnd_ = nullptr; ///< ウィンドウハンドル
};
}
