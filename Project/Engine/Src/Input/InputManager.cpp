#include "pch.h"
#include "InputManager.h"
#include <cassert>

#include "GamepadInput.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "WinApp/WinApp.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
InputManager::InputManager() = default;
InputManager::~InputManager() = default;

void InputManager::Initialize(HINSTANCE hInstance, HWND hwnd)
{
    HRESULT result;
    result = DirectInput8Create(
        hInstance,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()),
        nullptr);
    assert(SUCCEEDED(result));

    // 各デバイスを生成・初期化
    keyboard_ = std::make_unique<KeyboardInput>();
    keyboard_->Initialize(directInput_.Get(), hwnd);

    mouse_ = std::make_unique<MouseInput>();
    mouse_->Initialize(directInput_.Get(), hwnd);

    // 更新ループ用リストに登録（追加順で Update が呼ばれる）
    devices_.clear();
    devices_.push_back(keyboard_.get());
    devices_.push_back(mouse_.get());

    // パッドは 4 台分を作って毎フレーム見る。繋がっていない番号は何も返さない
    std::array<GamepadInput*, kMaxGamepads> pads{};
    for (int player = 0; player < kMaxGamepads; ++player) {
        gamepads_[player] = std::make_unique<GamepadInput>();
        gamepads_[player]->Initialize(static_cast<DWORD>(player));
        devices_.push_back(gamepads_[player].get());
        pads[player] = gamepads_[player].get();
    }

    // アクションベース入力クエリを初期化
    query_.Initialize(keyboard_.get(), mouse_.get(), pads);
}

void InputManager::Update()
{
    // 別のアプリを触っている間は取り込まない。
    // 取り込むと、切り替えた瞬間に押していたキーが押しっぱなしとして残り、
    // 戻ってきたときにメニューやキャラが勝手に動く
    if (!WinApp::IsAppActive()) {
        for (IInputDevice* device : devices_) {
            device->Reset();
        }
        return;
    }

    for (IInputDevice* device : devices_) {
        device->Update();
    }
}
}
