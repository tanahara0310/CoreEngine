#pragma once
#include "IInputDevice.h"
#include "InputQuery.h"

#include <dinput.h>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class KeyboardInput;
    class MouseInput;
    class GamepadInput;
}

/// @brief 入力管理クラス

namespace CoreEngine
{
class InputManager {
public:
    InputManager();
    ~InputManager();

    /// @brief 初期化
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// @brief 更新処理
    void Update();

    /// @brief アクションベース入力問い合わせへのアクセッサ
    /// @return InputQuery への参照
    InputQuery& GetQuery() { return query_; }
    const InputQuery& GetQuery() const { return query_; }

private:
    // DirectInputオブジェクト
    IDirectInput8* directInput_ = nullptr;

    // デバイス（型付きメンバ）
    std::unique_ptr<KeyboardInput> keyboard_;
    std::unique_ptr<MouseInput>    mouse_;
    std::unique_ptr<GamepadInput>  gamepad_;

    InputQuery query_;
};
}
