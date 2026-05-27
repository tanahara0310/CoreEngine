#pragma once
#include "IInputDevice.h"
#include "InputQuery.h"

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

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

    /// @brief 更新処理（登録済み全デバイスを一括更新）
    void Update();

    /// @brief アクションベース入力問い合わせへのアクセッサ
    InputQuery& GetQuery() { return query_; }
    const InputQuery& GetQuery() const { return query_; }

private:
    // DirectInputオブジェクト（ComPtrでリーク防止）
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;

    // デバイス所有（具体型 unique_ptr）
    std::unique_ptr<KeyboardInput> keyboard_;
    std::unique_ptr<MouseInput>    mouse_;
    std::unique_ptr<GamepadInput>  gamepad_;

    // 更新ループ用（非所有ポインタ、破棄順序は上の unique_ptr に依存）
    std::vector<IInputDevice*> devices_;

    InputQuery query_;
};
}
