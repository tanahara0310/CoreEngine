#pragma once
#include "IInputDevice.h"
#include "InputQuery.h"

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <vector>

// 前方宣言
namespace CoreEngine {
    class KeyboardInput;
    class MouseInput;
    class GamepadInput;
}

namespace CoreEngine
{
/// @brief 入力管理クラス
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
    /// @brief SDL のイベントを読み、つないだパッドを空いている番号へ割り当て、外したパッドを外す
    void PollGamepadEvents();

    /// @brief パッドを開いて番号へ割り当てる（XInput の番号が空いていればその番号）
    void AttachGamepad(uint32_t instanceId);

    /// @brief パッドの割り当てを外して閉じる
    void DetachGamepad(uint32_t instanceId);

    /// @brief 再生中で前面にいる間だけパッドを振動させる。再生を止めたら振動の指定を消す
    void UpdateVibration();

    // DirectInputオブジェクト（ComPtrでリーク防止）
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;

    // デバイス所有（具体型 unique_ptr）
    std::unique_ptr<KeyboardInput> keyboard_;
    std::unique_ptr<MouseInput>    mouse_;
    // 4 人まで同時に持つ。2 人目以降は問い合わせでプレイヤー番号を指定して使う
    std::array<std::unique_ptr<GamepadInput>, kMaxGamepads> gamepads_;

    // 更新ループ用（非所有ポインタ、破棄順序は上の unique_ptr に依存）
    std::vector<IInputDevice*> devices_;

    InputQuery query_;

    bool gamepadSystemReady_ = false;
    bool wasInPlayMode_ = false;
};
}
